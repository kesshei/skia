/*
 * Copyright 2017 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/sksl/lex/NFAtoDFA.h"
#include "src/sksl/lex/RegexParser.h"

#include <fstream>
#include <sstream>
#include <string>

/**
 * Processes a .lex file and produces .h and .cpp files which implement a lexical analyzer. The .lex
 * file is a text file with one token definition per line. Each line is of the form:
 * <TOKEN_NAME> = <pattern>
 * where <pattern> is either a regular expression (e.g [0-9]) or a double-quoted literal string.
 */

static constexpr const char* HEADER =
    "/*\n"
    " * Copyright 2017 Google Inc.\n"
    " *\n"
    " * Use of this source code is governed by a BSD-style license that can be\n"
    " * found in the LICENSE file.\n"
    " */\n"
    "/*****************************************************************************************\n"
    " ******************** This file was generated by sksllex. Do not edit. *******************\n"
    " *****************************************************************************************/\n";

void writeH(const DFA& dfa, const char* lexer, const char* token,
            const std::vector<std::string>& tokens, const char* hPath) {
    std::ofstream out(hPath);
    SkASSERT(out.good());
    out << HEADER;
    out << "#ifndef SKSL_" << lexer << "\n";
    out << "#define SKSL_" << lexer << "\n";
    out << "#include <cstddef>\n";
    out << "#include <cstdint>\n";
    out << "namespace SkSL {\n";
    out << "\n";
    out << "struct " << token << " {\n";
    out << "    enum class Kind {\n";
    for (const std::string& t : tokens) {
        out << "        TK_" << t << ",\n";
    }
    out << "        TK_NONE,\n";
    out << "    };\n";
    out << "\n";
    out << "    " << token << "()\n";
    out << "    : fKind(Kind::TK_NONE)\n";
    out << "    , fOffset(-1)\n";
    out << "    , fLength(-1) {}\n";
    out << "\n";
    out << "    " << token << "(Kind kind, int32_t offset, int32_t length)\n";
    out << "    : fKind(kind)\n";
    out << "    , fOffset(offset)\n";
    out << "    , fLength(length) {}\n";
    out << "\n";
    out << "    Kind fKind;\n";
    out << "    int fOffset;\n";
    out << "    int fLength;\n";
    out << "};\n";
    out << "\n";
    out << "class " << lexer << " {\n";
    out << "public:\n";
    out << "    void start(const char* text, int32_t length) {\n";
    out << "        fText = text;\n";
    out << "        fLength = length;\n";
    out << "        fOffset = 0;\n";
    out << "    }\n";
    out << "\n";
    out << "    " << token << " next();\n";
    out << "\n";
    out << "private:\n";
    out << "    const char* fText;\n";
    out << "    int32_t fLength;\n";
    out << "    int32_t fOffset;\n";
    out << "};\n";
    out << "\n";
    out << "} // namespace\n";
    out << "#endif\n";
}

void writeCPP(const DFA& dfa, const char* lexer, const char* token, const char* include,
              const char* cppPath) {
    std::ofstream out(cppPath);
    SkASSERT(out.good());
    out << HEADER;
    out << "#include \"" << include << "\"\n";
    out << "\n";
    out << "namespace SkSL {\n";
    out << "\n";

    size_t states = 0;
    for (const auto& row : dfa.fTransitions) {
        states = std::max(states, row.size());
    }
    // arbitrarily-chosen character which is greater than START_CHAR and should not appear in actual
    // input
    out << "static const uint8_t INVALID_CHAR = 18;";
    out << "static int8_t mappings[" << dfa.fCharMappings.size() << "] = {\n    ";
    const char* separator = "";
    for (int m : dfa.fCharMappings) {
        out << separator << std::to_string(m);
        separator = ", ";
    }
    out << "\n};\n";
    out << "static int16_t transitions[" << dfa.fTransitions.size() << "][" << states << "] = {\n";
    for (size_t c = 0; c < dfa.fTransitions.size(); ++c) {
        out << "    {";
        for (size_t j = 0; j < states; ++j) {
            if ((size_t) c < dfa.fTransitions.size() && j < dfa.fTransitions[c].size()) {
                out << " " << dfa.fTransitions[c][j] << ",";
            } else {
                out << " 0,";
            }
        }
        out << " },\n";
    }
    out << "};\n";
    out << "\n";

    out << "static int8_t accepts[" << states << "] = {";
    for (size_t i = 0; i < states; ++i) {
        if (i < dfa.fAccepts.size()) {
            out << " " << dfa.fAccepts[i] << ",";
        } else {
            out << " " << INVALID << ",";
        }
    }
    out << " };\n";
    out << "\n";

    out << token << " " << lexer << "::next() {\n";
    out << "    // note that we cheat here: normally a lexer needs to worry about the case\n";
    out << "    // where a token has a prefix which is not itself a valid token - for instance, \n";
    out << "    // maybe we have a valid token 'while', but 'w', 'wh', etc. are not valid\n";
    out << "    // tokens. Our grammar doesn't have this property, so we can simplify the logic\n";
    out << "    // a bit.\n";
    out << "    int32_t startOffset = fOffset;\n";
    out << "    if (startOffset == fLength) {\n";
    out << "        return " << token << "(" << token << "::Kind::TK_END_OF_FILE, startOffset,"
           "0);\n";
    out << "    }\n";
    out << "    int16_t state = 1;\n";
    out << "    for (;;) {\n";
    out << "        if (fOffset >= fLength) {\n";
    out << "            if (accepts[state] == -1) {\n";
    out << "                return Token(Token::Kind::TK_END_OF_FILE, startOffset, 0);\n";
    out << "            }\n";
    out << "            break;\n";
    out << "        }\n";
    out << "        uint8_t c = (uint8_t) fText[fOffset];";
    out << "        if (c <= 8 || c >= " << dfa.fCharMappings.size() << ") {";
    out << "            c = INVALID_CHAR;";
    out << "        }";
    out << "        int16_t newState = transitions[mappings[c]][state];\n";
    out << "        if (!newState) {\n";
    out << "            break;\n";
    out << "        }\n";
    out << "        state = newState;";
    out << "        ++fOffset;\n";
    out << "    }\n";
    out << "    Token::Kind kind = (" << token << "::Kind) accepts[state];\n";
    out << "    return " << token << "(kind, startOffset, fOffset - startOffset);\n";
    out << "}\n";
    out << "\n";
    out << "} // namespace\n";
}

void process(const char* inPath, const char* lexer, const char* token, const char* hPath,
             const char* cppPath) {
    NFA nfa;
    std::vector<std::string> tokens;
    tokens.push_back("END_OF_FILE");
    std::string line;
    std::ifstream in(inPath);
    while (std::getline(in, line)) {
        if (line.length() == 0) {
            continue;
        }
        if (line.length() >= 2 && line[0] == '/' && line[1] == '/') {
            continue;
        }
        std::istringstream split(line);
        std::string name, delimiter, pattern;
        if (split >> name >> delimiter >> pattern) {
            SkASSERT(split.eof());
            SkASSERT(name != "");
            SkASSERT(delimiter == "=");
            SkASSERT(pattern != "");
            tokens.push_back(name);
            if (pattern[0] == '"') {
                SkASSERT(pattern.size() > 2 && pattern[pattern.size() - 1] == '"');
                RegexNode node = RegexNode(RegexNode::kChar_Kind, pattern[1]);
                for (size_t i = 2; i < pattern.size() - 1; ++i) {
                    node = RegexNode(RegexNode::kConcat_Kind, node,
                                     RegexNode(RegexNode::kChar_Kind, pattern[i]));
                }
                nfa.addRegex(node);
            }
            else {
                nfa.addRegex(RegexParser().parse(pattern));
            }
        }
    }
    NFAtoDFA converter(&nfa);
    DFA dfa = converter.convert();
    writeH(dfa, lexer, token, tokens, hPath);
    writeCPP(dfa, lexer, token, (std::string("src/sksl/SkSL") + lexer + ".h").c_str(), cppPath);
}

int main(int argc, const char** argv) {
    if (argc != 6) {
        printf("usage: sksllex <input.lex> <lexername> <tokenname> <output.h> <output.cpp>\n");
        exit(1);
    }
    process(argv[1], argv[2], argv[3], argv[4], argv[5]);
    return 0;
}
