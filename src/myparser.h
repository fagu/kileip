/********************************************************************************
*   Copyright (C) Fabian Gundlach 2010 (320pointsguy@googlemail.com)            *
*********************************************************************************/

/***************************************************************************
*                                                                         *
*   This program is free software; you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License as published by  *
*   the Free Software Foundation; either version 2 of the License, or     *
*   (at your option) any later version.                                   *
*                                                                         *
***************************************************************************/

#ifndef MYPARSER_H
#define MYPARSER_H

#include <QString>
#include <QMap>
#include <QSharedDataPointer>
#include <stack>
#include <vector>
#include <memory>
#include <variant>

// helper type for visitors (see https://en.cppreference.com/w/cpp/utility/variant/visit)
template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

class Range {
public:
    int m_start, m_end;
    Range(int start, int end);
    QString source(const QString& text) const;
    // Whether text[start..(end-1)] == ref
    bool equal(const QString& text, const QString& ref) const;
};

#define PART(n) \
    class n; \
    typedef std::unique_ptr<n> U##n; \
    typedef n* P##n;

PART(TextPart);
PART(CommentPart);
PART(PrimitiveCommandPart);
PART(ArgsCommandPart);
PART(EnvironmentPart);

typedef std::variant<UTextPart,UCommentPart,UPrimitiveCommandPart,UArgsCommandPart,UEnvironmentPart > UPart;
typedef std::variant<PTextPart,PCommentPart,PPrimitiveCommandPart,PArgsCommandPart,PEnvironmentPart > PPart;
typedef std::variant<UPrimitiveCommandPart,UArgsCommandPart > UCommandPart;
typedef std::variant<PPrimitiveCommandPart,PArgsCommandPart > PCommandPart;

PPart to_ptr(const UPart& p);
PCommandPart to_ptr(const UCommandPart& p);
std::vector<PPart> to_ptr(const std::vector<UPart>& v);
std::optional<PPart> to_ptr(const std::optional<UPart>& v);

UPart generalize(UCommandPart&& p);
PPart generalize(const PCommandPart& p);

Range range(PPart part);
Range command_name_range(const PCommandPart& part);

class CommentPart {
private:
    Range m_range;
public:
    CommentPart(const Range& range);
    Range range() {return m_range;}
};

class PrimitiveCommandPart {
private:
    Range m_range;
public:
    PrimitiveCommandPart(const Range& range);
    Range range() {return m_range;}
};

class ArgsCommandPart {
private:
    Range m_range;
    Range m_name_range;
    std::vector<UPart > m_args;
    std::optional<UPart> m_optional;
public:
    ArgsCommandPart(const Range& range, const Range& name_range, std::vector< UPart >&& args, std::optional< UPart >&& optional);
    Range range() {return m_range;}
    Range name_range() {return m_name_range;}
    std::vector<PPart> args() {return to_ptr(m_args);}
    std::optional<PPart> optional() {return to_ptr(m_optional);}
};

class TextPart {
private:
    Range m_range;
    std::vector<UPart > m_parts;
public:
    TextPart(const Range& range, std::vector<UPart >&& parts);
    Range range() {return m_range;}
    std::vector<PPart> parts() {return to_ptr(m_parts);}
    QString sourceWithoutVoid(const QString& text) const;
};

class EnvironmentPart {
private:
    Range m_range;
    UCommandPart m_begin;
    std::optional<UCommandPart> m_end;
    UTextPart m_body;
public:
    EnvironmentPart(const Range& range, UCommandPart&& begin, std::optional<UCommandPart>&& end, UTextPart&& body);
    Range range() {return m_range;}
    PCommandPart begin() {return to_ptr(m_begin);}
    std::optional<PCommandPart> end() {if (m_end) return to_ptr(m_end.value()); else return std::nullopt;}
    PTextPart body() {return m_body.get();}
};

class Ending {
public:
    QString wanted;
    std::optional<UCommandPart> cp;
    Ending(QString w);
};

class Parser {
private:
    QString m_text;
    const QChar* m_textdata;
    int i;
    std::vector<Ending> endings;
    int code(QChar c);

public:
    Parser(const QString& text, int start);
    UTextPart parse();
private:
    UTextPart parseText();
    UCommentPart parseComment();
    UArgsCommandPart parseCommand();
    UPrimitiveCommandPart parsePrimitiveCommand();
    UEnvironmentPart parseEnvironment(UCommandPart&& begincommand);
    std::optional<UCommandPart> endingFound(UCommandPart&& cp);
    std::optional<UCommandPart> expectedEndingFound(UCommandPart&& cp);
    bool matches(QString wanted, PCommandPart cp);
};

struct SpecialEnvironment {
    QString endcommand;
    bool partofenvironment;
};

class Global {
public:
    static QMap<QString,int> commands;
    static QMap<QString,SpecialEnvironment> specialenvs;
    static void init();
};

#endif
