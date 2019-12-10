#ifndef PREVIEWPREPARER_H
#define PREVIEWPREPARER_H

#include <memory>
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
    int m_outermost_math_environment = -1;
    KTextEditor::Cursor m_mathstart;
public:
    bool in_document() {
        return m_in_document;
    }
    std::vector<QString>& environments() {
        return m_environments;
    }
    int outermost_math_environment() const {
        return m_outermost_math_environment;
    }
    KTextEditor::Cursor mathstart() const {
        return m_mathstart;
    }
    void start_math(const QString& environment_name, KTextEditor::Cursor cursor) {
        if (m_in_document) {
            if (m_outermost_math_environment == -1) {
                m_outermost_math_environment = m_environments.size();
                m_mathstart = cursor;
            }
            m_environments.push_back(environment_name);
        }
    }
    std::optional<KTextEditor::Range> finish_math(int environment, KTextEditor::Cursor cursor) {
        while((int)m_environments.size() > environment)
            m_environments.pop_back();
        if (m_outermost_math_environment == environment) {
            m_outermost_math_environment = -1;
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
        m_outermost_math_environment = -1;
    }
    void enter_document() {
        m_in_document = true;
    }
    friend bool operator==(const State& a, const State& b) {
        return a.m_in_document == b.m_in_document && a.m_environments == b.m_environments && a.m_outermost_math_environment == b.m_outermost_math_environment && (a.m_outermost_math_environment == -1 || a.m_mathstart == b.m_mathstart);
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
