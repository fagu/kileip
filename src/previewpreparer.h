#ifndef PREVIEWPREPARER_H
#define PREVIEWPREPARER_H

#include <memory>
#include <optional>
#include <vector>
#include <QString>
#include <QList>
#include <ktexteditor/range.h>

class MathEnv {
private:
    KTextEditor::Range m_range;
public:
    MathEnv(KTextEditor::Range range) : m_range(range) {}
    KTextEditor::Range range() const {
        return m_range;
    }
};

class State {
private:
    bool m_in_document = false;
    std::vector<QString> m_environments;
    KTextEditor::Cursor m_mathstart; // TODO MovingCursor?
public:
    bool in_document() {
        return m_in_document;
    }
    std::vector<QString>& environments() {
        return m_environments;
    }
    KTextEditor::Cursor mathstart() const {
        return m_mathstart;
    }
    void start_math(const QString& environment_name, KTextEditor::Cursor cursor) {
        if (m_in_document) {
            if (m_environments.empty()) {
                m_mathstart = cursor;
            }
            m_environments.push_back(environment_name);
        }
    }
    std::optional<KTextEditor::Range> finish_math(int environment, KTextEditor::Cursor cursor) {
        while((int)m_environments.size() > environment)
            m_environments.pop_back();
        if (environment == 0) {
            return KTextEditor::Range(m_mathstart, cursor);
        } else {
            return std::nullopt;
        }
    }
    std::optional<KTextEditor::Range> finish_math(const QString& environment_name, KTextEditor::Cursor cursor) {
        int environment = -1;
        bool correct = true;
        for (int e = (int)m_environments.size()-1; e >= 0; e--) {
            if (m_environments[e] == environment_name) {
                environment = e;
                break;
            }
        }
        correct = (environment == (int)m_environments.size()-1);
        if (environment == -1)
            return std::nullopt;
        else {
            auto r = finish_math(environment, cursor);
            if (correct)
                return r;
            else
                return std::nullopt;
        }
    }
    void destroy_math() {
        m_environments.clear();
    }
    void enter_document() {
        m_in_document = true;
    }
    void lineWrapped(const KTextEditor::Cursor &position) {
        if (m_mathstart.isValid() && m_mathstart >= position) {
            m_mathstart.setLine(m_mathstart.line() + 1);
        }
    }
    void lineUnwrapped(int linenr) {
        if (m_mathstart.isValid() && m_mathstart.line() >= linenr) {
            m_mathstart.setLine(m_mathstart.line() - 1);
        }
    }
    friend bool operator==(const State& a, const State& b) {
        return a.m_in_document == b.m_in_document && a.m_environments == b.m_environments && (a.m_environments.empty() || a.m_mathstart == b.m_mathstart);
    }
    friend bool operator!=(const State& a, const State& b) {
        return !(a == b);
    }
};

struct ParsedLine {
    std::vector<MathEnv> mathenvs;
    std::optional<KTextEditor::Cursor> begin_document;
};

std::pair<State, ParsedLine> parse(State state, int linenr, const QString& str);

#endif
