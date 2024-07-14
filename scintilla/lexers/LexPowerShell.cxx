// This file is part of Notepad4.
// See License.txt for details about distribution and modification.
//! Lexer for PowerShell.

#include <cassert>
#include <cstring>

#include <string>
#include <string_view>
#include <vector>

#include "ILexer.h"
#include "Scintilla.h"
#include "SciLexer.h"

#include "WordList.h"
#include "LexAccessor.h"
#include "Accessor.h"
#include "StyleContext.h"
#include "CharacterSet.h"
#include "StringUtils.h"
#include "LexerModule.h"
#include "LexerUtils.h"

using namespace Lexilla;

namespace {

//KeywordIndex++Autogenerated -- start of section automatically generated
enum {
	KeywordIndex_Keyword = 0,
	KeywordIndex_Type = 1,
	KeywordIndex_Cmdlet = 2,
	KeywordIndex_Alias = 3,
	KeywordIndex_PredefinedVariable = 4,
};
//KeywordIndex--Autogenerated -- end of section automatically generated

enum class KeywordType {
	None = SCE_POWERSHELL_DEFAULT,
	Label = SCE_POWERSHELL_LABEL,
	Class = SCE_POWERSHELL_CLASS,
	Enum = SCE_POWERSHELL_ENUM,
	Attribute = SCE_POWERSHELL_ATTRIBUTE,
	Function = SCE_POWERSHELL_FUNCTION_DEFINITION,
};

constexpr bool IsVariableCharacter(int ch) noexcept {
	return IsIdentifierCharEx(ch);
}

constexpr bool IsSpecialVariable(int ch) noexcept {
	return ch == '$' || ch == '?' || ch == '^' || ch == '_';
}

constexpr bool IsPsIdentifierChar(int ch) noexcept {
	return IsIdentifierCharEx(ch) || ch == '-';
}

constexpr bool PreferArrayIndex(int ch) noexcept {
	return ch == ')' || ch == ']' || IsIdentifierCharEx(ch);
}

constexpr bool IsSpaceEquiv(int state) noexcept {
	return state <= SCE_POWERSHELL_TASKMARKER;
}

void HighlightVariable(StyleContext &sc, std::vector<int> &nestedState) {
	const int state = sc.state;
	if (sc.chNext == '(') {
		sc.SetState((state == SCE_POWERSHELL_DEFAULT && nestedState.empty()) ? SCE_POWERSHELL_OPERATOR : SCE_POWERSHELL_OPERATOR2);
	} else if (sc.chNext == '{') {
		sc.SetState(SCE_POWERSHELL_BRACE_VARIABLE);
	} else if (IsVariableCharacter(sc.chNext) || IsSpecialVariable(sc.chNext)) {
		sc.SetState(SCE_POWERSHELL_VARIABLE);
	}
	if (state != sc.state) {
		sc.Forward();
		if (state != SCE_POWERSHELL_DEFAULT || !nestedState.empty()) {
			nestedState.push_back(state);
		}
	}
}

static_assert(DefaultNestedStateBaseStyle + 1 == SCE_POWERSHELL_STRING_DQ);
static_assert(DefaultNestedStateBaseStyle + 2 == SCE_POWERSHELL_HERE_STRING_DQ);

void ColourisePowerShellDoc(Sci_PositionU startPos, Sci_Position lengthDoc, int initStyle, LexerWordList keywordLists, Accessor &styler) {
	int lineStateLineType = 0;
	KeywordType kwType = KeywordType::None;
	int chBefore = 0;
	int chPrevNonWhite = 0;
	int stylePrevNonWhite = SCE_POWERSHELL_DEFAULT;
	int visibleChars = 0;
	int outerStyle = SCE_POWERSHELL_DEFAULT;
	std::vector<int> nestedState; // variable expansion "$()"

	StyleContext sc(startPos, lengthDoc, initStyle, styler);
	if (sc.currentLine > 0) {
		int lineState = styler.GetLineState(sc.currentLine - 1);
		lineState >>= 8;
		if (lineState) {
			UnpackLineState(lineState, nestedState);
		}
	}

	while (sc.More()) {
		switch (sc.state) {
		case SCE_POWERSHELL_OPERATOR:
		case SCE_POWERSHELL_OPERATOR2:
			sc.SetState(SCE_POWERSHELL_DEFAULT);
			break;

		case SCE_POWERSHELL_NUMBER:
			if (!IsDecimalNumber(sc.chPrev, sc.ch, sc.chNext)) {
				sc.SetState(SCE_POWERSHELL_DEFAULT);
			}
			break;

		case SCE_POWERSHELL_STRING_SQ:
			if (sc.ch == '\'') {
				if (sc.chNext == '\'') {
					outerStyle = SCE_POWERSHELL_STRING_SQ;
					sc.SetState(SCE_POWERSHELL_ESCAPECHAR);
					sc.Forward();
				} else {
					sc.ForwardSetState(SCE_POWERSHELL_DEFAULT);
				}
			}
			break;

		case SCE_POWERSHELL_HERE_STRING_SQ:
			if (sc.atLineStart && sc.Match('\'', '@')) {
				sc.Forward();
				sc.ForwardSetState(SCE_POWERSHELL_DEFAULT);
			}
			break;

		case SCE_POWERSHELL_STRING_DQ:
		case SCE_POWERSHELL_HERE_STRING_DQ:
			if (sc.ch == '`' || (sc.state == SCE_POWERSHELL_STRING_DQ && sc.Match('\"', '\"'))) {
				outerStyle = sc.state;
				sc.SetState(SCE_POWERSHELL_ESCAPECHAR);
				sc.Forward();
			} else if (sc.ch == '$') {
				HighlightVariable(sc, nestedState);
			} else if (sc.ch == '\"' && (sc.state != SCE_POWERSHELL_HERE_STRING_DQ || (sc.atLineStart && sc.chNext == '@'))) {
				if (sc.state == SCE_POWERSHELL_HERE_STRING_DQ) {
					sc.Forward();
				}
				sc.ForwardSetState(SCE_POWERSHELL_DEFAULT);
			}
			break;

		case SCE_POWERSHELL_ESCAPECHAR:
			sc.SetState(outerStyle);
			continue;

		case SCE_POWERSHELL_VARIABLE:
			if (sc.ch == ':' && IsVariableCharacter(sc.chNext)) {
				sc.ChangeState(SCE_POWERSHELL_VARIABLE_SCOPE);
				sc.SetState(SCE_POWERSHELL_OPERATOR);
				sc.ForwardSetState(SCE_POWERSHELL_VARIABLE);
			} else if (!IsVariableCharacter(sc.ch)) {
				const Sci_Position len = sc.LengthCurrent();
				if (len == 2) {
					if (IsSpecialVariable(sc.chPrev)) {
						sc.ChangeState(SCE_POWERSHELL_BUILTIN_VARIABLE);
					}
				} else if (len >= 4) {
					char s[64];
					sc.GetCurrentLowered(s, sizeof(s));
					const char *p = s;
					if (p[0] == '$' || p[0] == '@') {
						++p;
					}
					if (keywordLists[KeywordIndex_PredefinedVariable].InList(p)) {
						sc.ChangeState(SCE_POWERSHELL_BUILTIN_VARIABLE);
					}
				}
				sc.SetState(TryTakeAndPop(nestedState));
				continue;
			}
			break;

		case SCE_POWERSHELL_BRACE_VARIABLE:
			if (sc.ch == '`') {
				outerStyle = sc.state;
				sc.SetState(SCE_POWERSHELL_ESCAPECHAR);
				sc.Forward();
			} else if (sc.ch == '}') {
				sc.ForwardSetState(TryTakeAndPop(nestedState));
				continue;
			}
			break;

		case SCE_POWERSHELL_IDENTIFIER:
		case SCE_POWERSHELL_PARAMETER:
		case SCE_POWERSHELL_LABEL:
			if (!IsPsIdentifierChar(sc.ch)) {
				if (sc.state == SCE_POWERSHELL_IDENTIFIER) {
					char s[128];
					sc.GetCurrentLowered(s, sizeof(s));
					if (keywordLists[KeywordIndex_Keyword].InList(s)) {
						sc.ChangeState(SCE_POWERSHELL_KEYWORD);
						if (StrEqual(s, "class")) {
							kwType = KeywordType::Class;
						} else if (StrEqual(s, "enum")) {
							kwType = KeywordType::Enum;
						} else if (StrEqualsAny(s, "break", "continue")) {
							kwType = KeywordType::Label;
						} else if (StrEqualsAny(s, "function", "filter")) {
							kwType = KeywordType::Function;
						}
					} else if (keywordLists[KeywordIndex_Cmdlet].InList(s)) {
						sc.ChangeState(SCE_POWERSHELL_CMDLET);
					} else if (keywordLists[KeywordIndex_Alias].InList(s)) {
						sc.ChangeState(SCE_POWERSHELL_ALIAS);
					} else if (sc.ch != '.' && sc.ch != ':') {
						const int chNext = sc.GetLineNextChar();
						if (kwType == KeywordType::Attribute) {
							if (chBefore != '.' && keywordLists[KeywordIndex_Type].InList(s)) {
								sc.ChangeState(SCE_POWERSHELL_TYPE);
							} else if (chNext == '(') {
								sc.ChangeState(SCE_POWERSHELL_ATTRIBUTE);
							} else {
								sc.ChangeState(SCE_POWERSHELL_CLASS);
							}
						} else if (kwType != KeywordType::None) {
							sc.ChangeState(static_cast<int>(kwType));
						} else if (chNext == '(') {
							sc.ChangeState(SCE_POWERSHELL_FUNCTION);
						}
					}
					if (sc.state != SCE_POWERSHELL_KEYWORD && sc.ch != '.' && sc.ch != ':') {
						kwType = KeywordType::None;
					}
				}
				sc.SetState(SCE_POWERSHELL_DEFAULT);
			}
			break;

		case SCE_POWERSHELL_COMMENTLINE:
			if (sc.atLineStart) {
				sc.SetState(SCE_POWERSHELL_DEFAULT);
			}
			break;

		case SCE_POWERSHELL_DIRECTIVE:
			if (!IsAlpha(sc.ch)) {
				if (sc.ch <= ' ') {
					char s[16];
					sc.GetCurrentLowered(s, sizeof(s));
					if (StrEqualsAny(s, "#requires", "#region", "#endregion")) {
						lineStateLineType = 0;
						sc.SetState(SCE_POWERSHELL_COMMENTLINE);
						break;
					}
				}
				sc.ChangeState(SCE_POWERSHELL_COMMENTLINE);
			}
			break;

		case SCE_POWERSHELL_COMMENTBLOCK:
			if (sc.ch == '.' && visibleChars == 0 && IsAlpha(sc.chNext)) {
				sc.SetState(SCE_POWERSHELL_COMMENTTAG);
			} else if (sc.Match('#', '>')) {
				sc.Forward();
				sc.ForwardSetState(SCE_POWERSHELL_DEFAULT);
			}
			break;

		case SCE_POWERSHELL_COMMENTTAG:
			if (sc.ch <= ' ') {
				sc.SetState(SCE_POWERSHELL_COMMENTBLOCK);
			} else if (!IsAlpha(sc.ch)) {
				sc.ChangeState(SCE_POWERSHELL_COMMENTBLOCK);
				continue;
			}
			break;
		}

		if (sc.state == SCE_POWERSHELL_DEFAULT) {
			if (sc.ch == '#') {
				sc.SetState(SCE_POWERSHELL_COMMENTLINE);
				if (visibleChars == 0) {
					lineStateLineType = SimpleLineStateMaskLineComment;
					const int chNext = UnsafeLower(sc.chNext);
					if (chNext == 'r' || chNext == 'e') {
						sc.ChangeState(SCE_POWERSHELL_DIRECTIVE);
					}
				}
			} else if (sc.Match('<', '#')) {
				sc.SetState(SCE_POWERSHELL_COMMENTBLOCK);
				sc.Forward();
			} else if (sc.ch == '@') {
				if (sc.chNext == '\"') {
					sc.SetState(SCE_POWERSHELL_HERE_STRING_DQ);
					sc.Forward();
				} else if (sc.chNext == '\'') {
					sc.SetState(SCE_POWERSHELL_HERE_STRING_SQ);
					sc.Forward();
				} else if (IsVariableCharacter(sc.chNext)) {
					sc.SetState(SCE_POWERSHELL_VARIABLE);
				} else {
					sc.SetState(SCE_POWERSHELL_OPERATOR);
				}
			} else if (sc.ch == '\"') {
				sc.SetState(SCE_POWERSHELL_STRING_DQ);
			} else if (sc.ch == '\'') {
				sc.SetState(SCE_POWERSHELL_STRING_SQ);
			} else if (sc.ch == '$') {
				HighlightVariable(sc, nestedState);
			} else if (sc.ch == '`') {
				outerStyle = SCE_POWERSHELL_DEFAULT;
				sc.SetState(SCE_POWERSHELL_ESCAPECHAR);
				sc.Forward();
			} else if (IsNumberStartEx(sc.chPrev, sc.ch, sc.chNext)) {
				sc.SetState(SCE_POWERSHELL_NUMBER);
			} else if (sc.ch == '-' && IsIdentifierStartEx(sc.chNext)) {
				sc.SetState(SCE_POWERSHELL_PARAMETER);
			} else if (visibleChars == 0 && sc.ch == ':' && IsIdentifierStartEx(sc.chNext)) {
				sc.SetState(SCE_POWERSHELL_LABEL);
			} else if (IsIdentifierStartEx(sc.ch)) {
				chBefore = chPrevNonWhite;
				sc.SetState(SCE_POWERSHELL_IDENTIFIER);
			} else if (IsAGraphic(sc.ch)) {
				sc.SetState(SCE_POWERSHELL_OPERATOR);
				if (!nestedState.empty()) {
					sc.ChangeState(SCE_POWERSHELL_OPERATOR2);
					if (sc.ch == '(') {
						nestedState.push_back(SCE_POWERSHELL_DEFAULT);
					} else if (sc.ch == ')') {
						outerStyle = TakeAndPop(nestedState);
						sc.ForwardSetState(outerStyle);
						continue;
					}
				} else {
					if (kwType == KeywordType::None && sc.ch == '[') {
						if (visibleChars == 0
							|| stylePrevNonWhite == SCE_POWERSHELL_PARAMETER // operator
							|| !PreferArrayIndex(chPrevNonWhite)) {
							kwType = KeywordType::Attribute;
						}
					} else if (kwType == KeywordType::Attribute && (sc.ch == '(' || sc.ch == ']')) {
						kwType = KeywordType::None;
					}
				}
			}
		}

		if (!isspacechar(sc.ch)) {
			visibleChars++;
			if (!IsSpaceEquiv(sc.state)) {
				chPrevNonWhite = sc.ch;
				stylePrevNonWhite = sc.state;
			}
		}
		if (sc.atLineEnd) {
			int lineState = lineStateLineType;
			if (!nestedState.empty()) {
				lineState |= PackLineState(nestedState) << 8;
			}
			styler.SetLineState(sc.currentLine, lineState);
			lineStateLineType = 0;
			visibleChars = 0;
			kwType = KeywordType::None;
		}
		sc.Forward();
	}

	sc.Complete();
}

constexpr int GetLineCommentState(int lineState) noexcept {
	return lineState & SimpleLineStateMaskLineComment;
}

void FoldPowerShellDoc(Sci_PositionU startPos, Sci_Position lengthDoc, int initStyle, LexerWordList /*keywordLists*/, Accessor &styler) {
	const Sci_PositionU endPos = startPos + lengthDoc;
	Sci_Line lineCurrent = styler.GetLine(startPos);
	int levelCurrent = SC_FOLDLEVELBASE;
	int lineCommentPrev = 0;
	if (lineCurrent > 0) {
		levelCurrent = styler.LevelAt(lineCurrent - 1) >> 16;
		lineCommentPrev = GetLineCommentState(styler.GetLineState(lineCurrent - 1));
		const Sci_PositionU bracePos = CheckBraceOnNextLine(styler, lineCurrent - 1, SCE_POWERSHELL_OPERATOR, SCE_POWERSHELL_TASKMARKER);
		if (bracePos) {
			startPos = bracePos + 1; // skip the brace
		}
	}

	int levelNext = levelCurrent;
	int lineCommentCurrent = GetLineCommentState(styler.GetLineState(lineCurrent));
	Sci_PositionU lineStartNext = styler.LineStart(lineCurrent + 1);
	lineStartNext = sci::min(lineStartNext, endPos);

	int styleNext = styler.StyleAt(startPos);
	int style = initStyle;
	int visibleChars = 0;

	while (startPos < endPos) {
		const int stylePrev = style;
		style = styleNext;
		styleNext = styler.StyleAt(++startPos);

		switch (style) {
		case SCE_POWERSHELL_COMMENTBLOCK:
		case SCE_POWERSHELL_STRING_SQ:
		case SCE_POWERSHELL_HERE_STRING_SQ:
		case SCE_POWERSHELL_STRING_DQ:
		case SCE_POWERSHELL_HERE_STRING_DQ:
			if (style != stylePrev) {
				levelNext++;
			}
			if (style != styleNext) {
				levelNext--;
			}
			break;

		case SCE_POWERSHELL_OPERATOR:
		case SCE_POWERSHELL_OPERATOR2: {
			const char ch = styler[startPos - 1];
			if (ch == '{' || ch == '[' || ch == '(') {
				levelNext++;
			} else if (ch == '}' || ch == ']' || ch == ')') {
				levelNext--;
			}
		} break;
		}

		if (visibleChars == 0 && !IsSpaceEquiv(style)) {
			++visibleChars;
		}
		if (startPos == lineStartNext) {
			const int lineCommentNext = GetLineCommentState(styler.GetLineState(lineCurrent + 1));
			levelNext = sci::max(levelNext, SC_FOLDLEVELBASE);
			if (lineCommentCurrent) {
				levelNext += lineCommentNext - lineCommentPrev;
			} else if (visibleChars) {
				const Sci_PositionU bracePos = CheckBraceOnNextLine(styler, lineCurrent, SCE_POWERSHELL_OPERATOR, SCE_POWERSHELL_TASKMARKER);
				if (bracePos) {
					levelNext++;
					startPos = bracePos + 1; // skip the brace
					style = SCE_POWERSHELL_OPERATOR;
					styleNext = styler.StyleAt(startPos);
				}
			}

			const int levelUse = levelCurrent;
			int lev = levelUse | (levelNext << 16);
			if (levelUse < levelNext) {
				lev |= SC_FOLDLEVELHEADERFLAG;
			}
			styler.SetLevel(lineCurrent, lev);

			lineCurrent++;
			lineStartNext = styler.LineStart(lineCurrent + 1);
			lineStartNext = sci::min(lineStartNext, endPos);
			levelCurrent = levelNext;
			lineCommentPrev = lineCommentCurrent;
			lineCommentCurrent = lineCommentNext;
		}
	}
}

}

LexerModule lmPowerShell(SCLEX_POWERSHELL, ColourisePowerShellDoc, "powershell", FoldPowerShellDoc);
