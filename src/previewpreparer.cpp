#include "previewpreparer.h"

#include <set>

bool startsWithAt(const QString& text, const QString& word, int start) {
    if (start + word.size() > text.size())
        return false;
    for (int i = 0; i < word.size(); i++) {
        if (text[start+i] != word[i]) {
            return false;
        }
    }
    return true;
}

bool onlySpaces(const QString& text) {
    for (QChar c : text)
        if (!c.isSpace())
            return false;
    return true;
}

std::pair<State, ParsedLine> parse(State state, int linenr, const QString& line) {
    ParsedLine res;
    
    static std::set<QString> math_environment_names = {
        QStringLiteral("equation"),
        QStringLiteral("equation*"),
        QStringLiteral("align"),
        QStringLiteral("align*"),
        QStringLiteral("alignat"),
        QStringLiteral("alignat*"),
        QStringLiteral("multline"),
        QStringLiteral("multline*"),
        QStringLiteral("eqnarray"),
        QStringLiteral("eqnarray*"),
        QStringLiteral("gather"),
        QStringLiteral("gather*"),
        QStringLiteral("asy"),
        QStringLiteral("tikzpicture"),
    };

    if (onlySpaces(line)) {
        state.destroy_math();
    } else {
        for (int i = 0; i < line.size(); ) {
            QChar c = line[i];
            if (c == QLatin1Char('$')) { // Inline math
                int start = -1;
                for (int e = (int)state.environments().size()-1; e >= 0; e--) {
                    if (state.environments()[e] == QLatin1Char('$')) {
                        start = e;
                        break;
                    }
                }
                if (start == -1) { // Start a new inline math environment
                    state.start_math(QStringLiteral("$"), KTextEditor::Cursor(linenr, i));
                } else { // End inline math environment
                    auto r = state.finish_math(start, KTextEditor::Cursor(linenr, i + 1));
                    if (r)
                        res.mathenvs.emplace_back(r.value());
                }
                i += 1;
            } else if (c == QLatin1Char('\\')) {
                if (startsWithAt(line, QStringLiteral("["), i + 1)) { // \[
                    state.start_math(QStringLiteral("["), KTextEditor::Cursor(linenr, i));
                    i += 2;
                } else if (startsWithAt(line, QStringLiteral("]"), i + 1)) { // \]
                    auto r = state.finish_math(QStringLiteral("["), KTextEditor::Cursor(linenr, i + 2));
                    if (r)
                        res.mathenvs.emplace_back(r.value());
                    i += 2;
                } else if (startsWithAt(line, QStringLiteral("("), i + 1)) { // \(
                    state.start_math(QStringLiteral("("), KTextEditor::Cursor(linenr, i));
                    i += 2;
                } else if (startsWithAt(line, QStringLiteral(")"), i + 1)) { // \)
                    auto r = state.finish_math(QStringLiteral("("), KTextEditor::Cursor(linenr, i + 2));
                    if (r)
                        res.mathenvs.emplace_back(r.value());
                    i += 2;
                } else if (startsWithAt(line, QStringLiteral("begin"), i + 1)) { // \begin
                    int j = i + 6;
                    while(j < line.size() && line[j].isSpace())
                        j++;
                    if (j < line.size() && line[j] == QLatin1Char('{')) {
                        j++;
                        int k = j;
                        while(k < line.size() && line[k] != QLatin1Char('}'))
                            k++;
                        if (k < line.size()) {
                            QString en = line.mid(j, k-j);
                            if (en == QStringLiteral("document")) { // \begin{document}
                                if (!state.in_document()) {
                                    state.enter_document();
                                    res.begin_document = KTextEditor::Cursor(linenr, i);
                                }
                            }
                            if (math_environment_names.count(en)) { // \begin{equation}
                                if (startsWithAt(line, QStringLiteral("{")+en+QStringLiteral("}"), i + 6)) { // TODO make this efficient
                                    state.start_math(en, KTextEditor::Cursor(linenr, i));
                                }
                            }
                            k++;
                        }
                        j = k;
                    }
                    i = j;
                } else if (startsWithAt(line, QStringLiteral("end"), i + 1)) { // \end
                    int j = i + 4;
                    while(j < line.size() && line[j].isSpace())
                        j++;
                    if (j < line.size() && line[j] == QLatin1Char('{')) {
                        j++;
                        int k = j;
                        while(k < line.size() && line[k] != QLatin1Char('}'))
                            k++;
                        if (k < line.size()) {
                            QString en = line.mid(j, k-j);
                            if (math_environment_names.count(en)) { // \end{equation}
                                auto r = state.finish_math(en, KTextEditor::Cursor(linenr, k + 1));
                                if (r)
                                    res.mathenvs.emplace_back(r.value());
                            }
                            k++;
                        }
                        j = k;
                    }
                    i = j;
                } else {
                    i += 2;
                }
            } else if (c == QLatin1Char('%')) { // Ignore rest of line
                break;
            } else {
                i += 1;
            }
        }
    }
    
    // Check that all ranges are disjoint.
    for (int i = 0; i < (int)res.mathenvs.size(); i++) {
        assert(res.mathenvs[i].range().isValid());
        assert(!res.mathenvs[i].range().isEmpty());
        if (i > 0)
            assert(res.mathenvs[i].range().start() >= res.mathenvs[i-1].range().end());
    }
    
    return {std::move(state), std::move(res)};
}
