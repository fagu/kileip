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

#include "user.h"
#include <QDebug>
#include <QFile>
#include <QStandardPaths>
#include <QTime>


ParserResult::ParserResult(UTextPart&& doc, const QList<PPart>& mathgroups, QString text) : m_doc(std::move(doc)), m_mathgroups(mathgroups), m_text(text) {
}

TextPart* ParserResult::doc() {
    return m_doc.get();
}

QString ParserResult::text() {
    return m_text;
}

QList< PPart > ParserResult::mathgroups() {
    return m_mathgroups;
}

QMutex init_math_mutex;
QStringList User::mathbegincommands;
QStringList User::mathcommands;
QStringList User::mathenvs;

void User::initMath() {
    QMutexLocker lock(&init_math_mutex);
    if (mathenvs.size())
        return;
    QString commandsfilename = QStandardPaths::locate(QStandardPaths::DataLocation, "parser/maths.txt");
    commandsfilename = "/usr/share/kile/parser/maths.txt";
    qDebug() << "maths.txt at " << commandsfilename;
    QFile comfile(commandsfilename);
    if (!comfile.exists())
        qDebug() << "maths.txt missing";
    comfile.open(QIODevice::ReadOnly | QIODevice::Text);

    QTextStream in(&comfile);
    in.setCodec("UTF-8");
    int state = 0;
    while (!in.atEnd()) {
        QString line = in.readLine();
        if (line.at(0) == '#')
            continue;
        if (state == 0) {
            if (line == "!begincommand")
                state = 1;
            else {
                mathenvs << line;
            }
        } else if (state == 1) {
            if (line == "!command")
                state = 2;
            else
                mathbegincommands << line;
        } else {
            mathcommands << line;
        }
    }
    comfile.close();
}

std::shared_ptr<ParserResult> User::parse(const QString& text) {
    Parser p(text, 0);
    //QTime tim; tim.start();
    UTextPart newmain = p.parse();
    auto pa = document(newmain.get(), text);
    if (!pa)
        pa = newmain.get();
    QList<PPart> mathgroups = getMathgroups(pa, text);
    //qDebug() << "Parsing time:" << tim.elapsed()*0.001;
    return std::make_shared<ParserResult>(std::move(newmain), mathgroups, text);
}

PTextPart User::document(PTextPart ma, QString text) {
    for (unsigned int i = 0; i < ma->parts().size(); i++) {
        if (std::holds_alternative<PEnvironmentPart >(ma->parts()[i])) {
            PEnvironmentPart ep = std::get<PEnvironmentPart >(ma->parts()[i]);
            if (command_name_range(ep->begin()).source(text) == "\\begin" && std::get<PArgsCommandPart >(ep->begin())->args().size()) {
                if (std::get<PTextPart >(std::get<PArgsCommandPart >(ep->begin())->args()[0])->sourceWithoutVoid(text) == "document") {
                    return ep->body();
                }
            }
        }
    }
    return nullptr;
}

Range User::preamble(PTextPart ma, QString text) {
    int preend = text.length()-1;
    for (unsigned int i = 0; i < ma->parts().size(); i++) {
        if (std::holds_alternative<PEnvironmentPart >(ma->parts()[i])) {
            PEnvironmentPart ep = std::get<PEnvironmentPart >(ma->parts()[i]);
            if (command_name_range(ep->begin()).source(text) == "\\begin" && std::get<PArgsCommandPart >(ep->begin())->args().size()) {
                if (std::get<PTextPart >(std::get<PArgsCommandPart >(ep->begin())->args()[0])->sourceWithoutVoid(text) == "document") {
                    preend = ep->range().m_start-1;
                }
            }
        }
    }
    return Range(0, preend);
}

QList< PPart > User::getMathgroups(PPart part, QString text)
{
    initMath();
    QList<PPart> res;
    std::visit(overloaded{
        [](PCommentPart) {
        },
        [](PPrimitiveCommandPart) {
        },
        [&res,&text](PEnvironmentPart ep) {
            PCommandPart begin = ep->begin();
            QString name = command_name_range(begin).source(text);
            bool gef = false;
            if (mathbegincommands.contains(name))
                gef = true;
            else if (name == "\\begin") {
                PArgsCommandPart wa = std::get<PArgsCommandPart>(begin);
                if (wa->args().size()) {
                    QString envname = std::get<PTextPart>(wa->args()[0])->sourceWithoutVoid(text);
                    if (mathenvs.contains(envname))
                        gef = true;
                }
            }
            if (gef) {
                res += ep;
            } else {
//                 res += getMathgroups(ep->begin(), text);
                res += getMathgroups(ep->body(), text);
//                 res += getMathgroups(ep->end(), text);
            }
        },
        [&res,&text](PArgsCommandPart cop) {
            if (mathcommands.contains(cop->name_range().source(text))) {
                res += cop;
            } else {
                if (cop->optional().has_value())
                    res += getMathgroups(cop->optional().value(), text);
                for (unsigned int i = 0; i < cop->args().size(); i++) {
                    res += getMathgroups(cop->args()[i], text);
                }
            }
        },
        [&res,&text](const PTextPart& tp) {
            for (unsigned int i = 0; i < tp->parts().size(); i++) {
                res += getMathgroups(tp->parts()[i], text);
            }
        },
    }, part);
    return res;
}

User::User() : abort(false) {
}

User::~User() {
    {
        QMutexLocker lock(&mutex);
        abort = true;
        waitcond.wakeOne();
    }
    wait();
}

void User::run() {
    while(true) {
        QString parsetext;
        {
            QMutexLocker lock(&mutex);
            while(m_res && m_res->text() == newtext) {
                waitcond.wait(&mutex);
                if (abort)
                    return;
            }
            parsetext = newtext;
        }
        std::shared_ptr<ParserResult> res = parse(parsetext);
        {
            QMutexLocker lock(&mutex);
            m_res = res;
        }
        emit documentChanged();
    }
}

std::shared_ptr<ParserResult> User::data() {
    QMutexLocker lock(&mutex);
    return m_res;
}



void User::textChanged(QString ntext) {
    QMutexLocker lock(&mutex);
    newtext = ntext;
    waitcond.wakeOne();
}
