/* regexp.c */

#include "internal.h"
#include "regexp.h"
#include <setjmp.h>
#include <stdio.h>
#include <ctype.h>


#if ( defined BB_GREP || defined BB_FIND  || defined BB_SED)

/* This also tries to find a needle in a haystack, but uses
 * real regular expressions....  The fake regular expression
 * version of find_match lives in utility.c.  Using this version
 * will add 3.9k to busybox...
 *  -Erik Andersen
 */
extern int find_match(char *haystack, char *needle, int ignoreCase)
{
    int status;
    struct regexp*  re;
    re = regcomp( needle);
    status = regexec(re, haystack, FALSE, ignoreCase);
    free( re);
    return( status);
}

#if defined BB_SED
/* This performs substitutions after a regexp match has been found.  
 * The new string is returned.  It is malloc'ed, and do must be freed. */
extern int replace_match(char *haystack, char *needle, char *newNeedle, int ignoreCase)
{
    int status;
    struct regexp*  re;
    char *s, buf[BUF_SIZE], *d = buf;

    re = regcomp( needle);
    status = regexec(re, haystack, FALSE, ignoreCase);
    if (status==TRUE) {
	s=haystack;

	do {
	    /* copy stuff from before the match */
	    while (s < re->startp[0])
		*d++ = *s++;
	    /* substitute for the matched part */
	    regsub(re, newNeedle, d);
	    s = re->endp[0];
	    d += strlen(d);
	} while (regexec(re, s, FALSE, ignoreCase) == TRUE);
	 /* copy stuff from after the match */
	while ( (*d++ = *s++) ) {}
	d[-1] = '\n';
	d[0] = '\0';
	strcpy(haystack, buf);
    }
    free( re);
    return( status);
}
#endif


/* code swiped from elvis-tiny 1.4 (a clone of vi) and adjusted to 
 * suit the needs of busybox by Erik Andersen.
 *
 * From the README:
 * "Elvis is freely redistributable, in either source form or executable form.
 * There are no restrictions on how you may use it".
 * Elvis was written by Steve Kirkendall <kirkenda@cs.pdx.edu>
 *
 *
 * This file contains the code that compiles regular expressions and executes
 * them.  It supports the same syntax and features as vi's regular expression
 * code.  Specifically, the meta characters are:
 *	^	matches the beginning of a line
 *	$	matches the end of a line
 *	\<	matches the beginning of a word
 *	\>	matches the end of a word
 *	.	matches any single character
 *	[]	matches any character in a character class
 *	\(	delimits the start of a subexpression
 *	\)	delimits the end of a subexpression
 *	*	repeats the preceding 0 or more times
 * NOTE: You cannot follow a \) with a *.
 *
 * The physical structure of a compiled RE is as follows:
 *	- First, there is a one-byte value that says how many character classes
 *	  are used in this regular expression
 *	- Next, each character class is stored as a bitmap that is 256 bits
 *	  (32 bytes) long.
 *	- A mixture of literal characters and compiled meta characters follows.
 *	  This begins with M_BEGIN(0) and ends with M_END(0).  All meta chars
 *	  are stored as a \n followed by a one-byte code, so they take up two
 *	  bytes apiece.  Literal characters take up one byte apiece.  \n can't
 *	  be used as a literal character.
 *
 */



static char *previous;	/* the previous regexp, used when null regexp is given */
static char *previous1;	/* a copy of the text from the previous substitution for regsub()*/


/* These are used to classify or recognize meta-characters */
#define META		'\0'
#define BASE_META(m)	((m) - 256)
#define INT_META(c)	((c) + 256)
#define IS_META(m)	((m) >= 256)
#define IS_CLASS(m)	((m) >= M_CLASS(0) && (m) <= M_CLASS(9))
#define IS_START(m)	((m) >= M_START(0) && (m) <= M_START(9))
#define IS_END(m)	((m) >= M_END(0) && (m) <= M_END(9))
#define IS_CLOSURE(m)	((m) >= M_SPLAT && (m) <= M_QMARK)
#define ADD_META(s,m)	(*(s)++ = META, *(s)++ = BASE_META(m))
#define GET_META(s)	(*(s) == META ? INT_META(*++(s)) : *s)

/* These are the internal codes used for each type of meta-character */
#define M_BEGLINE	256		/* internal code for ^ */
#define M_ENDLINE	257		/* internal code for $ */
#define M_BEGWORD	258		/* internal code for \< */
#define M_ENDWORD	259		/* internal code for \> */
#define M_ANY		260		/* internal code for . */
#define M_SPLAT		261		/* internal code for * */
#define M_PLUS		262		/* internal code for \+ */
#define M_QMARK		263		/* internal code for \? */
#define M_CLASS(n)	(264+(n))	/* internal code for [] */
#define M_START(n)	(274+(n))	/* internal code for \( */
#define M_END(n)	(284+(n))	/* internal code for \) */

/* These are used during compilation */
static int	class_cnt;	/* used to assign class IDs */
static int	start_cnt;	/* used to assign start IDs */
static int	end_stk[NSUBEXP];/* used to assign end IDs */
static int	end_sp;
static char	*retext;	/* points to the text being compiled */

/* error-handling stuff */
jmp_buf	errorhandler;
#define FAIL(why)	fprintf(stderr, why); longjmp(errorhandler, 1)




/* This function builds a bitmap for a particular class */
/* text -- start of the class */
/* bmap -- the bitmap */
static char *makeclass(char* text, char* bmap)
{
	int		i;
	int		complement = 0;


	/* zero the bitmap */
	for (i = 0; bmap && i < 32; i++)
	{
		bmap[i] = 0;
	}

	/* see if we're going to complement this class */
	if (*text == '^')
	{
		text++;
		complement = 1;
	}

	/* add in the characters */
	while (*text && *text != ']')
	{
		/* is this a span of characters? */
		if (text[1] == '-' && text[2])
		{
			/* spans can't be backwards */
			if (text[0] > text[2])
			{
				FAIL("Backwards span in []");
			}

			/* add each character in the span to the bitmap */
			for (i = text[0]; bmap && i <= text[2]; i++)
			{
				bmap[i >> 3] |= (1 << (i & 7));
			}

			/* move past this span */
			text += 3;
		}
		else
		{
			/* add this single character to the span */
			i = *text++;
			if (bmap)
			{
				bmap[i >> 3] |= (1 << (i & 7));
			}
		}
	}

	/* make sure the closing ] is missing */
	if (*text++ != ']')
	{
		FAIL("] missing");
	}

	/* if we're supposed to complement this class, then do so */
	if (complement && bmap)
	{
		for (i = 0; i < 32; i++)
		{
			bmap[i] = ~bmap[i];
		}
	}

	return text;
}




/* This function gets the next character or meta character from a string.
 * The pointer is incremented by 1, or by 2 for \-quoted characters.  For [],
 * a bitmap is generated via makeclass() (if re is given), and the
 * character-class text is skipped.
 */
static int gettoken(sptr, re)
	char	**sptr;
	regexp	*re;
{
	int	c;

	c = **sptr;
	++*sptr;
	if (c == '\\')
	{
		c = **sptr;
		++*sptr;
		switch (c)
		{
		  case '<':
			return M_BEGWORD;

		  case '>':
			return M_ENDWORD;

		  case '(':
			if (start_cnt >= NSUBEXP)
			{
				FAIL("Too many \\(s");
			}
			end_stk[end_sp++] = start_cnt;
			return M_START(start_cnt++);

		  case ')':
			if (end_sp <= 0)
			{
				FAIL("Mismatched \\)");
			}
			return M_END(end_stk[--end_sp]);

		  case '*':
			return M_SPLAT;

		  case '.':
			return M_ANY;

		  case '+':
			return M_PLUS;

		  case '?':
			return M_QMARK;

		  default:
			return c;
		}
	}
	else {
		switch (c)
		{
		  case '^':
			if (*sptr == retext + 1)
			{
				return M_BEGLINE;
			}
			return c;

		  case '$':
			if (!**sptr)
			{
				return M_ENDLINE;
			}
			return c;

		  case '.':
			return M_ANY;

		  case '*':
			return M_SPLAT;

		  case '[':
			/* make sure we don't have too many classes */
			if (class_cnt >= 10)
			{
				FAIL("Too many []s");
			}

			/* process the character list for this class */
			if (re)
			{
				/* generate the bitmap for this class */
				*sptr = makeclass(*sptr, re->program + 1 + 32 * class_cnt);
			}
			else
			{
				/* skip to end of the class */
				*sptr = makeclass(*sptr, (char *)0);
			}
			return M_CLASS(class_cnt++);

		  default:
			return c;
		}
	}
	/*NOTREACHED*/
}




/* This function calculates the number of bytes that will be needed for a
 * compiled RE.  Its argument is the uncompiled version.  It is not clever
 * about catching syntax errors; that is done in a later pass.
 */
static unsigned calcsize(text)
	char		*text;
{
	unsigned	size;
	int		token;

	retext = text;
	class_cnt = 0;
	start_cnt = 1;
	end_sp = 0;
	size = 5;
	while ((token = gettoken(&text, (regexp *)0)) != 0)
	{
		if (IS_CLASS(token))
		{
			size += 34;
		}
		else if (IS_META(token))
		{
			size += 2;
		}
		else
		{
			size++;
		}
	}

	return size;
}



/*---------------------------------------------------------------------------*/


/* This function checks for a match between a character and a token which is
 * known to represent a single character.  It returns 0 if they match, or
 * 1 if they don't.
 */
static int match1(regexp* re, char ch, int token, int ignoreCase)
{
	if (!ch)
	{
		/* the end of a line can't match any RE of width 1 */
		return 1;
	}
	if (token == M_ANY)
	{
		return 0;
	}
	else if (IS_CLASS(token))
	{
		if (re->program[1 + 32 * (token - M_CLASS(0)) + (ch >> 3)] & (1 << (ch & 7)))
			return 0;
	}
	else if (ch == token
		|| (ignoreCase==TRUE && isupper(ch) && tolower(ch) == token))
	{
		return 0;
	}
	return 1;
}



/* This function checks characters up to and including the next closure, at
 * which point it does a recursive call to check the rest of it.  This function
 * returns 0 if everything matches, or 1 if something doesn't match.
 */
/* re   -- the regular expression */
/* str  -- the string */
/* prog -- a portion of re->program, an compiled RE */
/* here -- a portion of str, the string to compare it to */
static int match(regexp* re, char* str, char* prog, char* here, int ignoreCase)
{
	int		token;
	int		nmatched;
	int		closure;

	for (token = GET_META(prog); !IS_CLOSURE(token); prog++, token = GET_META(prog))
	{
		switch (token)
		{
		/*case M_BEGLINE: can't happen; re->bol is used instead */
		  case M_ENDLINE:
			if (*here)
				return 1;
			break;

		  case M_BEGWORD:
			if (here != str &&
			   (here[-1] == '_' ||
			     (isascii(here[-1]) && isalnum(here[-1]))))
				return 1;
			break;

		  case M_ENDWORD:
			if ((here[0] == '_' || isascii(here[0])) && isalnum(here[0]))
				return 1;
			break;

		  case M_START(0):
		  case M_START(1):
		  case M_START(2):
		  case M_START(3):
		  case M_START(4):
		  case M_START(5):
		  case M_START(6):
		  case M_START(7):
		  case M_START(8):
		  case M_START(9):
			re->startp[token - M_START(0)] = (char *)here;
			break;

		  case M_END(0):
		  case M_END(1):
		  case M_END(2):
		  case M_END(3):
		  case M_END(4):
		  case M_END(5):
		  case M_END(6):
		  case M_END(7):
		  case M_END(8):
		  case M_END(9):
			re->endp[token - M_END(0)] = (char *)here;
			if (token == M_END(0))
			{
				return 0;
			}
			break;

		  default: /* literal, M_CLASS(n), or M_ANY */
			if (match1(re, *here, token, ignoreCase) != 0)
				return 1;
			here++;
		}
	}

	/* C L O S U R E */

	/* step 1: see what we have to match against, and move "prog" to point
	 * the the remainder of the compiled RE.
	 */
	closure = token;
	prog++, token = GET_META(prog);
	prog++;

	/* step 2: see how many times we can match that token against the string */
	for (nmatched = 0;
	     (closure != M_QMARK || nmatched < 1) && *here && match1(re, *here, token, ignoreCase) == 0;
	     nmatched++, here++)
	{
	}

	/* step 3: try to match the remainder, and back off if it doesn't */
	while (nmatched >= 0 && match(re, str, prog, here, ignoreCase) != 0)
	{
		nmatched--;
		here--;
	}

	/* so how did it work out? */
	if (nmatched >= ((closure == M_PLUS) ? 1 : 0))
		return 0;
	return 1;
}


/* This function compiles a regexp. */
extern regexp *regcomp(char* text)
{
	int		needfirst;
	unsigned	size;
	int		token;
	int		peek;
	char		*build;
	regexp		*re;


	/* prepare for error handling */
	re = (regexp *)0;
	if (setjmp(errorhandler))
	{
		if (re)
		{
			free(re);
		}
		return (regexp *)0;
	}

	/* if an empty regexp string was given, use the previous one */
	if (*text == 0)
	{
		if (!previous)
		{
			FAIL("No previous RE");
		}
		text = previous;
	}
	else /* non-empty regexp given, so remember it */
	{
		if (previous)
			free(previous);
		previous = (char *)malloc((unsigned)(strlen(text) + 1));
		if (previous)
			strcpy(previous, text);
	}

	/* allocate memory */
	class_cnt = 0;
	start_cnt = 1;
	end_sp = 0;
	retext = text;
	size = calcsize(text) + sizeof(regexp);
	re = (regexp *)malloc((unsigned)size);

	if (!re)
	{
		FAIL("Not enough memory for this RE");
	}

	/* compile it */
	build = &re->program[1 + 32 * class_cnt];
	re->program[0] = class_cnt;
	for (token = 0; token < NSUBEXP; token++)
	{
		re->startp[token] = re->endp[token] = (char *)0;
	}
	re->first = 0;
	re->bol = 0;
	re->minlen = 0;
	needfirst = 1;
	class_cnt = 0;
	start_cnt = 1;
	end_sp = 0;
	retext = text;
	for (token = M_START(0), peek = gettoken(&text, re);
	     token;
	     token = peek, peek = gettoken(&text, re))
	{
		/* special processing for the closure operator */
		if (IS_CLOSURE(peek))
		{
			/* detect misuse of closure operator */
			if (IS_START(token))
			{
				FAIL("* or \\+ or \\? follows nothing");
			}
			else if (IS_META(token) && token != M_ANY && !IS_CLASS(token))
			{
				FAIL("* or \\+ or \\? can only follow a normal character or . or []");
			}

			/* it is okay -- make it prefix instead of postfix */
			ADD_META(build, peek);

			/* take care of "needfirst" - is this the first char? */
			if (needfirst && peek == M_PLUS && !IS_META(token))
			{
				re->first = token;
			}
			needfirst = 0;

			/* we used "peek" -- need to refill it */
			peek = gettoken(&text, re);
			if (IS_CLOSURE(peek))
			{
				FAIL("* or \\+ or \\? doubled up");
			}
		}
		else if (!IS_META(token))
		{
			/* normal char is NOT argument of closure */
			if (needfirst)
			{
				re->first = token;
				needfirst = 0;
			}
			re->minlen++;
		}
		else if (token == M_ANY || IS_CLASS(token))
		{
			/* . or [] is NOT argument of closure */
			needfirst = 0;
			re->minlen++;
		}

		/* the "token" character is not closure -- process it normally */
		if (token == M_BEGLINE)
		{
			/* set the BOL flag instead of storing M_BEGLINE */
			re->bol = 1;
		}
		else if (IS_META(token))
		{
			ADD_META(build, token);
		}
		else
		{
			*build++ = token;
		}
	}

	/* end it with a \) which MUST MATCH the opening \( */
	ADD_META(build, M_END(0));
	if (end_sp > 0)
	{
		FAIL("Not enough \\)s");
	}

	return re;
}




/* This function searches through a string for text that matches an RE. */
/* re  -- the compiled regexp to search for */
/* str -- the string to search through */
/* bol -- does str start at the beginning of a line? (boolean) */
/* ignoreCase -- ignoreCase or not */
extern int regexec(struct regexp* re, char* str, int bol, int ignoreCase)
{
	char	*prog;	/* the entry point of re->program */
	int	len;	/* length of the string */
	char	*here;

	/* if must start at the beginning of a line, and this isn't, then fail */
	if (re->bol && bol==TRUE)
	{
		return FALSE;
	}

	len = strlen(str);
	prog = re->program + 1 + 32 * re->program[0];

	/* search for the RE in the string */
	if (re->bol)
	{
		/* must occur at BOL */
		if ((re->first
			&& match1(re, *(char *)str, re->first, ignoreCase))/* wrong first letter? */
		 || len < re->minlen			/* not long enough? */
		 || match(re, (char *)str, prog, str, ignoreCase))	/* doesn't match? */
			return FALSE;			/* THEN FAIL! */
	}
	else if (ignoreCase == FALSE)
	{
		/* can occur anywhere in the line, noignorecase */
		for (here = (char *)str;
		     (re->first && re->first != *here)
			|| match(re, (char *)str, prog, here, ignoreCase);
		     here++, len--)
		{
			if (len < re->minlen)
				return FALSE;
		}
	}
	else
	{
		/* can occur anywhere in the line, ignorecase */
		for (here = (char *)str;
		     (re->first && match1(re, *here, (int)re->first, ignoreCase))
			|| match(re, (char *)str, prog, here, ignoreCase);
		     here++, len--)
		{
			if (len < re->minlen)
				return FALSE;
		}
	}

	/* if we didn't fail, then we must have succeeded */
	return TRUE;
}




#if defined BB_SED
/* This performs substitutions after a regexp match has been found.  */
extern void regsub(regexp* re, char* src, char* dst)
{
	char	*cpy;
	char	*end;
	char	c;
	char		*start;
	int		mod;

	mod = 0;

	start = src;
	while ((c = *src++) != '\0')
	{
		/* recognize any meta characters */
		if (c == '&')
		{
			cpy = re->startp[0];
			end = re->endp[0];
		}
		else if (c == '~')
		{
			cpy = previous1;
			if (cpy)
				end = cpy + strlen(cpy);
		}
		else
		if (c == '\\')
		{
			c = *src++;
			switch (c)
			{
			  case '0':
			  case '1':
			  case '2':
			  case '3':
			  case '4':
			  case '5':
			  case '6':
			  case '7':
			  case '8':
			  case '9':
				/* \0 thru \9 mean "copy subexpression" */
				c -= '0';
				cpy = re->startp[(int)c];
				end = re->endp[(int)c];
				break;
			  case 'U':
			  case 'u':
			  case 'L':
			  case 'l':
				/* \U and \L mean "convert to upper/lowercase" */
				mod = c;
				continue;

			  case 'E':
			  case 'e':
				/* \E ends the \U or \L */
				mod = 0;
				continue;
			  case '&':
				/* "\&" means "original text" */
				*dst++ = c;
				continue;

			  case '~':
				/* "\~" means "previous text, if any" */
				*dst++ = c;
				continue;
			  default:
				/* ordinary char preceded by backslash */
				*dst++ = c;
				continue;
			}
		}
		else
		{
			/* ordinary character, so just copy it */
			*dst++ = c;
			continue;
		}

		/* Note: to reach this point in the code, we must have evaded
		 * all "continue" statements.  To do that, we must have hit
		 * a metacharacter that involves copying.
		 */

		/* if there is nothing to copy, loop */
		if (!cpy)
			continue;

		/* copy over a portion of the original */
		while (cpy < end)
		{
			switch (mod)
			{
			  case 'U':
			  case 'u':
				/* convert to uppercase */
				if (isascii(*cpy) && islower(*cpy))
				{
					*dst++ = toupper(*cpy);
					cpy++;
				}
				else
				{
					*dst++ = *cpy++;
				}
				break;

			  case 'L':
			  case 'l':
				/* convert to lowercase */
				if (isascii(*cpy) && isupper(*cpy))
				{
					*dst++ = tolower(*cpy);
					cpy++;
				}
				else
				{
					*dst++ = *cpy++;
				}
				break;

			  default:
				/* copy without any conversion */
				*dst++ = *cpy++;
			}

			/* \u and \l end automatically after the first char */
			if (mod && (mod == 'u' || mod == 'l'))
			{
				mod = 0;
			}
		}
	}
	*dst = '\0';

	/* remember what text we inserted this time */
	if (previous1)
		free(previous1);
	previous1 = (char *)malloc((unsigned)(strlen(start) + 1));
	if (previous1)
		strcpy(previous1, start);
}
#endif

#endif /* BB_REGEXP */


