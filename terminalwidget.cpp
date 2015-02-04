/*  Copyright (C) 2008 e_k (e_k@users.sourceforge.net)

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to
    the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
    Boston, MA 02110-1301, USA.
*/

// Own includes
#include "colortables.h"
#include "session.h"
#include "screen.h"
#include "screenwindow.h"
#include "emulation.h"
#include "terminaldisplay.h"
#include "keyboardtranslator.h"
#include "colorscheme.h"
#include "searchbar.h"
#include "terminalwidget.h"

// Qt includes
#include <QLayout>
#include <QBoxLayout>
#include <QtDebug>
#include <QDir>
#include <QMessageBox>

#define STEP_ZOOM 1

struct TermWidgetImpl {
    TermWidgetImpl(QWidget* parent = 0);

    TerminalDisplay *m_terminalDisplay;
    Session *m_session;

    Session* createSession(QWidget* parent);
    TerminalDisplay* createTerminalDisplay(Session *session, QWidget* parent);
};

TermWidgetImpl::TermWidgetImpl(QWidget* parent) {
    this->m_session = createSession(parent);
    this->m_terminalDisplay = createTerminalDisplay(this->m_session, parent);
}


Session *TermWidgetImpl::createSession(QWidget* parent) {
    Session *session = new Session(parent);
    session->setTitle(Session::NameRole, "TerminalWidget");

    /* Thats a freaking bad idea!!!!
     * /bin/bash is not there on every system
     * better set it to the current $SHELL
     * Maybe you can also make a list available and then let the widget-owner decide what to use.
     * By setting it to $SHELL right away we actually make the first filecheck obsolete.
     * But as iam not sure if you want to do anything else ill just let both checks in and set this to $SHELL anyway.
     */
    //session->setProgram("/bin/bash");
    session->setProgram(getenv("SHELL"));

    QStringList args("");
    session->setArguments(args);
    session->setAutoClose(true);
    session->setCodec(QTextCodec::codecForName("UTF-8"));
    session->setFlowControlEnabled(true);
    session->setHistoryType(HistoryTypeBuffer(1000));
    session->setDarkBackground(true);
    session->setKeyBindings("");
    return session;
}

TerminalDisplay *TermWidgetImpl::createTerminalDisplay(Session *session, QWidget* parent) {
    TerminalDisplay* display = new TerminalDisplay(parent);
    display->setBellMode(TerminalDisplay::NotifyBell);
    display->setTerminalSizeHint(true);
    display->setTripleClickMode(TerminalDisplay::SelectWholeLine);
    display->setTerminalSizeStartup(true);
    display->setRandomSeed(session->sessionId() * 31);
    return display;
}


TerminalWidget::TerminalWidget(QWidget *parent, bool startSession)
    : QWidget(parent) {
    initialize(startSession);
}

void TerminalWidget::selectionChanged(bool textSelected) {
    emit copyAvailable(textSelected);
}

void TerminalWidget::find() {
    search(true, false);
}

void TerminalWidget::findNext() {
    search(true, true);
}

void TerminalWidget::findPrevious() {
    search(false, false);
}

void TerminalWidget::search(bool forwards, bool next) {
    int startColumn, startLine;
    
    if (next) {
        m_impl->m_terminalDisplay->screenWindow()->screen()->getSelectionEnd(startColumn, startLine);
        startColumn++;
    } else {
        m_impl->m_terminalDisplay->screenWindow()->screen()->getSelectionStart(startColumn, startLine);
    }

    QRegExp regExp(m_searchBar->searchText());
    regExp.setPatternSyntax(m_searchBar->useRegularExpression() ? QRegExp::RegExp : QRegExp::FixedString);
    regExp.setCaseSensitivity(m_searchBar->matchCase() ? Qt::CaseSensitive : Qt::CaseInsensitive);

    HistorySearch *historySearch =
            new HistorySearch(m_impl->m_session->emulation(), regExp, forwards, startColumn, startLine, this);
    connect(historySearch, SIGNAL(matchFound(int, int, int, int)), this, SLOT(matchFound(int, int, int, int)));
    connect(historySearch, SIGNAL(noMatchFound()), this, SLOT(noMatchFound()));
    connect(historySearch, SIGNAL(noMatchFound()), m_searchBar, SLOT(noMatchFound()));
    historySearch->search();
}

void TerminalWidget::matchFound(int startColumn, int startLine, int endColumn, int endLine) {
    ScreenWindow* sw = m_impl->m_terminalDisplay->screenWindow();
    qDebug() << "Scroll to" << startLine;
    sw->scrollTo(startLine);
    sw->setTrackOutput(false);
    sw->notifyOutputChanged();
    sw->setSelectionStart(startColumn, startLine - sw->currentLine(), false);
    sw->setSelectionEnd(endColumn, endLine - sw->currentLine());
}

void TerminalWidget::noMatchFound() {
    m_impl->m_terminalDisplay->screenWindow()->clearSelection();
}

int TerminalWidget::getShellPID() {
    return m_impl->m_session->processId();
}

void TerminalWidget::changeDir(const QString & dir) {
    /*
       this is a very hackish way of trying to determine if the shell is in
       the foreground before attempting to change the directory.  It may not
       be portable to anything other than Linux.
    */
    QString strCmd;
    strCmd.setNum(getShellPID());
    strCmd.prepend("ps -j ");
    strCmd.append(" | tail -1 | awk '{ print $5 }' | grep -q \\+");
    int retval = system(strCmd.toStdString().c_str());

    if (!retval) {
        QString cmd = "cd " + dir + "\n";
        pasteText(cmd);
    }
}

QSize TerminalWidget::sizeHint() const {
    QSize size = m_impl->m_terminalDisplay->sizeHint();
    size.rheight() = 150;
    return size;
}

void TerminalWidget::startShellProgram() {
    if ( m_impl->m_session->isRunning() ) {
        return;
    }

    m_impl->m_session->run();
}

void TerminalWidget::initialize(bool startSession) {
    m_layout = new QVBoxLayout();
    m_layout->setMargin(0);
    setLayout(m_layout);
    
    m_impl = new TermWidgetImpl(this);
    m_impl->m_terminalDisplay->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding);
    m_layout->addWidget(m_impl->m_terminalDisplay);

    connect(m_impl->m_session, SIGNAL(bellRequest(QString)), m_impl->m_terminalDisplay, SLOT(bell(QString)));
    connect(m_impl->m_terminalDisplay, SIGNAL(notifyBell(QString)), this, SIGNAL(bell(QString)));

    connect(m_impl->m_session, SIGNAL(activity()), this, SIGNAL(activity()));
    connect(m_impl->m_session, SIGNAL(silence()), this, SIGNAL(silence()));

    // That's OK, FilterChain's dtor takes care of UrlFilter.
    UrlFilter *urlFilter = new UrlFilter();
    connect(urlFilter, SIGNAL(activated(QUrl)), this, SIGNAL(urlActivated(QUrl)));
    m_impl->m_terminalDisplay->filterChain()->addFilter(urlFilter);

    m_searchBar = new SearchBar(this);
    m_searchBar->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Maximum);
    connect(m_searchBar, SIGNAL(searchCriteriaChanged()), this, SLOT(find()));
    connect(m_searchBar, SIGNAL(findNext()), this, SLOT(findNext()));
    connect(m_searchBar, SIGNAL(findPrevious()), this, SLOT(findPrevious()));
    m_layout->addWidget(m_searchBar);
    m_searchBar->hide();

    if (startSession && m_impl->m_session) {
        m_impl->m_session->run();
    }

    this->setFocus( Qt::OtherFocusReason );
    this->setFocusPolicy( Qt::WheelFocus );
    m_impl->m_terminalDisplay->resize(this->size());

    this->setFocusProxy(m_impl->m_terminalDisplay);
    connect(m_impl->m_terminalDisplay, SIGNAL(copyAvailable(bool)),
            this, SLOT(selectionChanged(bool)));
    connect(m_impl->m_terminalDisplay, SIGNAL(termGetFocus()),
            this, SIGNAL(termGetFocus()));
    connect(m_impl->m_terminalDisplay, SIGNAL(termLostFocus()),
            this, SIGNAL(termLostFocus()));
    connect(m_impl->m_terminalDisplay, SIGNAL(keyPressedSignal(QKeyEvent *)),
            this, SIGNAL(termKeyPressed(QKeyEvent *)));
    //    m_impl->m_terminalDisplay->setSize(80, 40);

    QFont font = QApplication::font();
    font.setFamily("Monospace");
    font.setPointSize(10);
    font.setStyleHint(QFont::TypeWriter);
    setTerminalFont(font);
    m_searchBar->setFont(font);

    setScrollBarPosition(NoScrollBar);

    m_impl->m_session->addView(m_impl->m_terminalDisplay);

    connect(m_impl->m_session, SIGNAL(finished()), this, SLOT(sessionFinished()));
}


TerminalWidget::~TerminalWidget() {
    delete m_impl;
    emit destroyed();
}

void TerminalWidget::setTerminalFont(const QFont &font) {
    if (!m_impl->m_terminalDisplay)
        return;
    m_impl->m_terminalDisplay->setVTFont(font);
}

QFont TerminalWidget::terminalFont() {
    if (!m_impl->m_terminalDisplay)
        return QFont();
    return m_impl->m_terminalDisplay->getVTFont();
}

void TerminalWidget::setTerminalOpacity(qreal level) {
    if (!m_impl->m_terminalDisplay)
        return;

    m_impl->m_terminalDisplay->setOpacity(level);
}

void TerminalWidget::setShellProgram(const QString &shellProgram) {
    if (!m_impl->m_session)
        return;
    m_impl->m_session->setProgram(shellProgram);
}

void TerminalWidget::setWorkingDirectory(const QString& dir) {
    if (!m_impl->m_session)
        return;
    m_impl->m_session->setInitialWorkingDirectory(dir);
}

QString TerminalWidget::workingDirectory() {
    if (!m_impl->m_session)
        return QString();

#ifdef Q_OS_LINUX
    // Christian Surlykke: On linux we could look at /proc/<pid>/cwd which should be a link to current
    // working directory (<pid>: process id of the shell). I don't know about BSD.
    // Maybe we could just offer it when running linux, for a start.
    QDir d(QString("/proc/%1/cwd").arg(getShellPID()));
    if (!d.exists())
    {
        qDebug() << "Cannot find" << d.dirName();
        goto fallback;
    }
    return d.canonicalPath();
#endif

fallback:
    // fallback, initial WD
    return m_impl->m_session->initialWorkingDirectory();
}

void TerminalWidget::setShellProgramArguments(const QStringList &arguments) {
    if (!m_impl->m_session)
        return;
    m_impl->m_session->setArguments(arguments);
}

void TerminalWidget::setTextCodec(QTextCodec *codec) {
    if (!m_impl->m_session)
        return;
    m_impl->m_session->setCodec(codec);
}

void TerminalWidget::setColorScheme(const QString& origName) {
    const ColorScheme *cs = 0;

    const bool isFile = QFile::exists(origName);
    const QString& name = isFile ?
                QFileInfo(origName).baseName() :
                origName;

    if(!availableColorSchemes().contains(name)) {
        if (isFile) {
            if (ColorSchemeManager::instance()->loadCustomColorScheme(origName))
                cs = ColorSchemeManager::instance()->findColorScheme(name);
            else
                qWarning () << Q_FUNC_INFO
                            << "cannot load color scheme from"
                            << origName;
        }

        if (!cs)
            cs = ColorSchemeManager::instance()->defaultColorScheme();
    }
    else
        cs = ColorSchemeManager::instance()->findColorScheme(name);

    if (! cs)
    {
        QMessageBox::information(this,
                                 tr("Color Scheme Error"),
                                 tr("Cannot load color scheme: %1").arg(name));
        return;
    }
    ColorEntry table[TABLE_COLORS];
    cs->getColorTable(table);
    m_impl->m_terminalDisplay->setColorTable(table);
}

QStringList TerminalWidget::availableColorSchemes() {
    QStringList ret;
    foreach (const ColorScheme* cs, ColorSchemeManager::instance()->allColorSchemes())
        ret.append(cs->name());
    return ret;
}

void TerminalWidget::setSize(int h, int v) {
    if (!m_impl->m_terminalDisplay)
        return;
    m_impl->m_terminalDisplay->setSize(h, v);
}

void TerminalWidget::setHistorySize(int lines) {
    if (lines < 0)
        m_impl->m_session->setHistoryType(HistoryTypeFile());
    else
        m_impl->m_session->setHistoryType(HistoryTypeBuffer(lines));
}

void TerminalWidget::setScrollBarPosition(ScrollBarPosition pos) {
    if (!m_impl->m_terminalDisplay)
        return;
    m_impl->m_terminalDisplay->setScrollBarPosition((TerminalDisplay::ScrollBarPosition)pos);
}

void TerminalWidget::scrollToEnd() {
    if (!m_impl->m_terminalDisplay)
        return;
    m_impl->m_terminalDisplay->scrollToEnd();
}

void TerminalWidget::pasteText(const QString &text) {
    m_impl->m_session->sendText(text);
}

void TerminalWidget::resizeEvent(QResizeEvent*) {
    m_impl->m_terminalDisplay->resize(this->size());
}

void TerminalWidget::sessionFinished() {
    emit finished();
}

void TerminalWidget::copyClipboard() {
    m_impl->m_terminalDisplay->copyClipboard();
}

void TerminalWidget::pasteClipboard() {
    m_impl->m_terminalDisplay->pasteClipboard();
}

void TerminalWidget::pasteSelection() {
    m_impl->m_terminalDisplay->pasteSelection();
}

void TerminalWidget::setZoom(int step) {
    if (!m_impl->m_terminalDisplay)
        return;
    
    QFont font = m_impl->m_terminalDisplay->getVTFont();
    
    font.setPointSize(font.pointSize() + step);
    setTerminalFont(font);
}

void TerminalWidget::zoomIn() {
    setZoom(STEP_ZOOM);
}

void TerminalWidget::zoomOut() {
    setZoom(-STEP_ZOOM);
}

void TerminalWidget::setKeyBindings(const QString & kb) {
    m_impl->m_session->setKeyBindings(kb);
}

void TerminalWidget::clear() {
    m_impl->m_session->emulation()->reset();
    m_impl->m_session->refresh();
    m_impl->m_session->clearHistory();
}

void TerminalWidget::setFlowControlEnabled(bool enabled) {
    m_impl->m_session->setFlowControlEnabled(enabled);
}

QStringList TerminalWidget::availableKeyBindings() {
    return KeyboardTranslatorManager::instance()->allTranslators();
}

QString TerminalWidget::keyBindings() {
    return m_impl->m_session->keyBindings();
}

void TerminalWidget::toggleShowSearchBar() {
    m_searchBar->isHidden() ? m_searchBar->show() : m_searchBar->hide();
}

bool TerminalWidget::flowControlEnabled(void) {
    return m_impl->m_session->flowControlEnabled();
}

void TerminalWidget::setFlowControlWarningEnabled(bool enabled) {
    if (flowControlEnabled()) {
        // Do not show warning label if flow control is disabled
        m_impl->m_terminalDisplay->setFlowControlWarningEnabled(enabled);
    }
}

void TerminalWidget::setEnvironment(const QStringList& environment) {
    m_impl->m_session->setEnvironment(environment);
}

void TerminalWidget::setMotionAfterPasting(int action) {
    m_impl->m_terminalDisplay->setMotionAfterPasting((MotionAfterPasting) action);
}

int TerminalWidget::historyLinesCount() {
    return m_impl->m_terminalDisplay->screenWindow()->screen()->getHistLines();
}

int TerminalWidget::screenColumnsCount() {
    return m_impl->m_terminalDisplay->screenWindow()->screen()->getColumns();
}

void TerminalWidget::setSelectionStart(int row, int column) {
    m_impl->m_terminalDisplay->screenWindow()->screen()->setSelectionStart(column, row, true);
}

void TerminalWidget::setSelectionEnd(int row, int column) {
    m_impl->m_terminalDisplay->screenWindow()->screen()->setSelectionEnd(column, row);
}

void TerminalWidget::getSelectionStart(int& row, int& column) {
    m_impl->m_terminalDisplay->screenWindow()->screen()->getSelectionStart(column, row);
}

void TerminalWidget::getSelectionEnd(int& row, int& column) {
    m_impl->m_terminalDisplay->screenWindow()->screen()->getSelectionEnd(column, row);
}

QString TerminalWidget::selectedText(bool preserveLineBreaks) {
    return m_impl->m_terminalDisplay->screenWindow()->screen()->selectedText(preserveLineBreaks);
}

void TerminalWidget::setMonitorActivity(bool monitor) {
    m_impl->m_session->setMonitorActivity(monitor);
}

void TerminalWidget::setMonitorSilence(bool monitor) {
    m_impl->m_session->setMonitorSilence(monitor);
}

void TerminalWidget::setSilenceTimeout(int seconds) {
    m_impl->m_session->setMonitorSilenceSeconds(seconds);
}

Filter::HotSpot* TerminalWidget::getHotSpotAt(const QPoint &pos) const {
    int row = 0, column = 0;
    m_impl->m_terminalDisplay->getCharacterPosition(pos, row, column);
    return getHotSpotAt(row, column);
}

Filter::HotSpot* TerminalWidget::getHotSpotAt(int row, int column) const {
    return m_impl->m_terminalDisplay->filterChain()->hotSpotAt(row, column);
}

