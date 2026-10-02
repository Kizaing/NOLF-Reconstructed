// Jupiter runtime/shared/src/conparse.cpp (ConParse is declared in the Talon SDK ltbasedefs.h).
#include <string.h>
#include <ctype.h>
#include "ltbasedefs.h"

LTBOOL cp_Parse(char *pCommand, const char **pNewCommandPos, char *argBuffer, char **argPointers, int *nArgs);

// FUNCTION: LITHTECH 0x004201f0
LTBOOL ConParse::Parse()
{
	if(!m_pCommandPos)
		return LTFALSE;

	if(!cp_Parse(m_pCommandPos, (const char **)&m_pCommandPos, m_ArgBuffer, m_Args, &m_nArgs))
	{
		m_pCommandPos = LTNULL;
	}

	return LTTRUE;
}

// FUNCTION: LITHTECH 0x00420230
LTBOOL ConParse::ParseFind(char *pLookFor, LTBOOL bCaseSensitive, uint32 minTokens)
{
	LTBOOL equal;

	// Must have at least one token, otherwise it can't find anything.
	if(minTokens == 0)
		minTokens = 1;
	
	while(Parse())
	{
		if(m_nArgs >= (int)minTokens)
		{
			if(bCaseSensitive)
				equal = (strcmp(m_Args[0], pLookFor) == 0);
			else
				equal = (stricmp(m_Args[0], pLookFor) == 0);
		
			if(equal)
			{
				return LTTRUE;
			}
		}
	}

	return LTFALSE;
}


// Talon's tokenizer predates Jupiter's: parenthesised and quoted runs are parsed by
// separate (recursive) helpers and characters are appended through cp_AddChar.

#define QUOTE_CHAR		'\"'
#define SPECIAL_CHAR	'%'

enum GNTResult
{
	GNT_NoToken=0,
	GNT_GotToken,
	GNT_GotSemicolon
};

static GNTResult cp_GetNextToken(const char* &pCurPos, char* &pTokenPos);
static void cp_AddChar(char* &pTokenPos, char *pToken, char theChar);
static LTBOOL cp_ParseParen(const char* &pCurPos, char* &pTokenPos, char *pToken);
static LTBOOL cp_ParseQuote(const char* &pCurPos, char* &pTokenPos, char *pToken);

// FUNCTION: LITHTECH 0x004202e0
LTBOOL cp_Parse(char *pCommand, const char **pNewCommandPos, char *argBuffer, char **argPointers, int *nArgs)
{
	GNTResult status;
	char *pCurArgBufferPos, *pToken;
	const char *pCurPos;


	// Parse.
	pCurPos = pCommand;
	pCurArgBufferPos = argBuffer;
	*nArgs = 0;

	do
	{
		pToken = pCurArgBufferPos;
		status = cp_GetNextToken(pCurPos, pCurArgBufferPos);

		if(status == GNT_NoToken)
		{
			// All done..
			return 0;
		}
		else if(status == GNT_GotToken)
		{
			argPointers[*nArgs] = pToken;

			++(*nArgs);
			if(*nArgs >= PARSE_MAXARGS)
				break;
		}
		else if(status == GNT_GotSemicolon)
		{
			// Got a semicolon.. finish up and tell them that there are more.
			*pNewCommandPos = pCurPos;
			return 1;
		}
	}
	while(status != GNT_NoToken);

	return 0;
}

// FUNCTION: LITHTECH 0x00420360
static GNTResult cp_GetNextToken(const char* &pCurPos, char* &pTokenPos)
{
	char *pToken;
	char curChar;
	LTBOOL bEnd = LTFALSE;

	// Skip spaces.
	while(pCurPos[0] == ' ')
		pCurPos++;

	// Is there even a string?
	if(pCurPos[0] == 0)
		return GNT_NoToken;

	pToken = pTokenPos;
	while(!bEnd)
	{
		// Get the char.
		curChar = *pCurPos;

		// End of string?
		if(curChar == 0)
		{
			bEnd = LTTRUE;
		}
		else if(curChar == SPECIAL_CHAR)
		{
			// Just add the next character to the string.
			pCurPos++;
			curChar = *pCurPos;
			if(curChar == 0)
			{
				bEnd = LTTRUE;
			}
			else
			{
				cp_AddChar(pTokenPos, pToken, curChar);
				pCurPos++;
			}
		}
		else if(curChar == ';' || iscntrl(curChar))
		{
			// If this is the first character, then return the fact that it's a semicolon.
			*pTokenPos = 0;
			++pTokenPos;
			if(pToken[0] == 0)
			{
				// Only increment it if it's a full semicolon delimiter, so that
				// next time around parsing, it'll skip past the semicolon.
				++pCurPos;
				return GNT_GotSemicolon;
			}
			else
			{
				return GNT_GotToken;
			}
		}
		else if(curChar == '(')
		{
			if(pTokenPos == pToken)
			{
				++pCurPos;
				cp_ParseParen(pCurPos, pTokenPos, pToken);
			}
			bEnd = LTTRUE;
		}
		else if(curChar == QUOTE_CHAR)
		{
			if(pTokenPos == pToken)
			{
				++pCurPos;
				cp_ParseQuote(pCurPos, pTokenPos, pToken);
			}
			bEnd = LTTRUE;
		}
		else if(curChar == ' ')
		{
			bEnd = LTTRUE;
		}
		else
		{
			cp_AddChar(pTokenPos, pToken, curChar);
			pCurPos++;
		}
	}

	*pTokenPos = 0;
	++pTokenPos;
	if(pToken[0] == 0)
		return GNT_NoToken;
	else
		return GNT_GotToken;
}

// FUNCTION: LITHTECH 0x00420470
static void cp_AddChar(char* &pTokenPos, char *pToken, char theChar)
{
	*pTokenPos = theChar;
	if((pTokenPos - pToken) >= PARSE_MAXARGLEN)
	{
		*pTokenPos = 0; // Just truncate it.
		--pTokenPos; // Decrement it so it doesn't overflow but it eats up the rest of the token.
	}

	++pTokenPos;
}

// FUNCTION: LITHTECH 0x004204a0
static LTBOOL cp_ParseParen(const char* &pCurPos, char* &pTokenPos, char *pToken)
{
	while(*pCurPos != 0 && *pCurPos != ')')
	{
		if(*pCurPos == SPECIAL_CHAR)
		{
			cp_AddChar(pTokenPos, pToken, *pCurPos++);
			if(*pCurPos != 0)
				cp_AddChar(pTokenPos, pToken, *pCurPos++);
		}
		else if(*pCurPos == '(')
		{
			// Nested parenthesis.
			cp_AddChar(pTokenPos, pToken, *pCurPos++);
			cp_ParseParen(pCurPos, pTokenPos, pToken);
			cp_AddChar(pTokenPos, pToken, ')');
		}
		else
		{
			cp_AddChar(pTokenPos, pToken, *pCurPos++);
		}
	}

	if(*pCurPos == ')')
	{
		++pCurPos;
		if(pTokenPos == pToken)
			cp_AddChar(pTokenPos, pToken, ' ');

		return LTTRUE;
	}

	return LTFALSE;
}

// FUNCTION: LITHTECH 0x00420540
static LTBOOL cp_ParseQuote(const char* &pCurPos, char* &pTokenPos, char *pToken)
{
	while(*pCurPos != 0 && *pCurPos != QUOTE_CHAR)
	{
		if(*pCurPos == SPECIAL_CHAR)
		{
			cp_AddChar(pTokenPos, pToken, *pCurPos++);
			if(*pCurPos != 0)
				cp_AddChar(pTokenPos, pToken, *pCurPos++);
		}
		else
		{
			cp_AddChar(pTokenPos, pToken, *pCurPos++);
		}
	}

	if(*pCurPos == QUOTE_CHAR)
	{
		++pCurPos;
		if(pTokenPos == pToken)
			cp_AddChar(pTokenPos, pToken, ' ');

		return LTTRUE;
	}

	return LTFALSE;
}
