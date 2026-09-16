// SPDX-License-Identifier: GPL-3.0-or-later
#include "touchcontroller.h"
#include "qtgui/freqctrl.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDockWidget>
#include <QGridLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenuBar>
#include <QMenu>
#include <QSet>
#include <functional>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QScroller>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWindow>
#include <cmath>
#include <limits>

namespace {
constexpr int controlHeight = 38;

const char *touchStyle = R"(
 QWidget { font-size: 13px; background-color: #101923; color: #ecf3fa; }
 QWidget#touchShell, QDialog { background: #101923; color: #ecf3fa; }
 QLabel { color: #d0deeb; background: transparent; }
 QLabel#pageTitle { font-size: 15px; font-weight: bold; }
 QLabel#touchHint { color: #8ea5ba; font-size: 12px; }
 QPushButton, QToolButton, QComboBox, QAbstractSpinBox, QLineEdit {
   color: #eaf3fc; background: #223243; border: 2px solid #35495d;
   border-radius: 6px; min-height: 26px; padding: 4px;
 }
 QPushButton:pressed, QPushButton:checked { background: #235f68; }
 QPushButton:focus, QToolButton:focus, QComboBox:focus, QAbstractSpinBox:focus,
 QLineEdit:focus, QCheckBox:focus, QRadioButton:focus, QSlider:focus,
 QAbstractItemView:focus { border: 2px solid #66e1ce; }
 QPushButton:disabled, QToolButton:disabled { color: #8092a3; background: #1a2632; }
 QPushButton#touchFrequency { font-size: 26px; font-weight: bold; color: #74ead7; }
 QComboBox::drop-down { width: 24px; border: none; }
 QComboBox QAbstractItemView { background: #223243; color: #ecf3fa; selection-background-color: #235f68; }
 QAbstractItemView { background: #172432; color: #ecf3fa; alternate-background-color: #223243; }
 QAbstractItemView::item { min-height: 32px; }
 QHeaderView::section { background: #223243; color: #ecf3fa; padding: 6px; }
 QScrollArea { border: none; background: #101923; }
 QScrollBar:vertical { width: 20px; background: #172432; }
 QScrollBar:horizontal { height: 20px; background: #172432; }
 QScrollBar::handle { background: #58758f; min-height: 36px; min-width: 36px; border-radius: 6px; }
 QCheckBox, QRadioButton { color: #ecf3fa; min-height: 34px; spacing: 7px; }
 QCheckBox::indicator, QRadioButton::indicator { width: 22px; height: 22px; }
 QSlider { min-height: 34px; }
 QSlider::groove:horizontal { background: #35495d; height: 8px; border-radius: 4px; }
 QSlider::handle:horizontal { background: #66e1ce; width: 24px; margin: -10px 0; border-radius: 7px; }
 QGroupBox { color: #ecf3fa; border: 1px solid #35495d; border-radius: 7px; margin-top: 16px; padding-top: 10px; }
 QGroupBox::title { subcontrol-origin: margin; left: 10px; }
 QTabBar::tab { background: #223243; color: #ecf3fa; padding: 9px; }
 QTabBar::tab:selected { background: #235f68; }
)";

QPushButton *button(const QString &text, QWidget *parent, const QString &name = {})
{
    auto *b = new QPushButton(text, parent);
    b->setObjectName(name);
    b->setMinimumHeight(controlHeight);
    b->setFocusPolicy(Qt::StrongFocus);
    b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    return b;
}

QScrollArea *scrollable(QWidget *content, QWidget *parent)
{
    auto *scroll = new QScrollArea(parent);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(content);
    // Touch scrolling leaves mouse/pen manipulation of receiver controls intact.
    QScroller::grabGesture(scroll->viewport(), QScroller::TouchGesture);
    return scroll;
}

QSize handheldSize(QMainWindow *window)
{
    auto *screen = window->windowHandle() ? window->windowHandle()->screen() : QGuiApplication::primaryScreen();
    return screen ? screen->availableGeometry().size().boundedTo(QSize(640, 480)) : QSize(640, 480);
}

bool editor(QWidget *w)
{
    return qobject_cast<QLineEdit *>(w) || qobject_cast<QAbstractSpinBox *>(w)
        || qobject_cast<QComboBox *>(w) || qobject_cast<QAbstractItemView *>(w)
        || qobject_cast<QSlider *>(w) || w->inherits("CFreqCtrl");
}
}

bool TouchController::startupEnabled()
{
    const auto args = QCoreApplication::arguments();
    if (args.contains("--desktop")) return false;
    if (args.contains("--touch")) return true;
    QSettings settings;
    return settings.value("touch/enabled", false).toBool();
}

TouchController::TouchController(QMainWindow *host, CFreqCtrl *freq, QWidget *spectrum,
                                 const QList<QDockWidget *> &docks)
    : QObject(host), window(host), frequency(freq), plot(spectrum)
{
    for (auto *dock : docks) panels.append({dock, dock->widget(), nullptr, true});
    toggle = new QAction(tr("Touch interface"), this);
    toggle->setObjectName("actionTouchInterface");
    toggle->setCheckable(true);
    toggle->setShortcut(QKeySequence("Ctrl+Shift+T"));
    window->addAction(toggle);
    connect(toggle, &QAction::toggled, this, &TouchController::setEnabled);
    connect(frequency, &CFreqCtrl::newFrequency, this, [this] { refreshFrequency(); });
    qApp->installEventFilter(this);
}

TouchController::~TouchController()
{
    qApp->removeEventFilter(this);
}

void TouchController::setEnabled(bool on)
{
    if (enabled == on) return;
    enabled = on;
    { QSignalBlocker block(toggle); toggle->setChecked(on); }
    QSettings settings;
    settings.setValue("touch/enabled", on);
    if (on) build(); else restore();
}

void TouchController::rememberDesktopLayout(const QByteArray &geometry, const QByteArray &state)
{
    if (!geometry.isEmpty()) desktopGeometry = geometry;
    if (!state.isEmpty()) desktopState = state;
}

void TouchController::prepareForShutdown()
{
    // Restore before upstream saves its desktop geometry and deletes its widgets.
    // The preferred startup mode stays in a separate application setting.
    if (enabled) {
        enabled = false;
        restore();
    }
}

QComboBox *TouchController::mirrorCombo(QComboBox *source, const QString &name)
{
    auto *combo = new QComboBox(shell);
    combo->setObjectName(name);
    combo->setMinimumHeight(controlHeight);
    combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    combo->setModel(source->model());
    combo->setCurrentIndex(source->currentIndex());
    connect(combo, QOverload<int>::of(&QComboBox::activated), source, [source](int i) {
        source->setCurrentIndex(i);
        // Gqrx deliberately uses activated(), not currentIndexChanged(), for DSP changes.
        emit source->activated(i);
    });
    connect(source, QOverload<int>::of(&QComboBox::currentIndexChanged), combo,
            [combo](int i) { QSignalBlocker block(combo); combo->setCurrentIndex(i); });
    return combo;
}

void TouchController::build()
{
    nativeDialogsDisabled = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
    desktopState = window->saveState();
    desktopGeometry = window->saveGeometry();
    menuVisible = !window->menuBar()->isHidden();
    statusVisible = !window->statusBar()->isHidden();
    for (auto &panel : panels) adaptControls(panel.content);
    adaptControls(window->centralWidget());
    desktop = window->takeCentralWidget();
    desktop->setParent(window);
    desktop->hide();
    for (auto *bar : window->findChildren<QToolBar *>(QString(), Qt::FindDirectChildrenOnly)) bar->hide();
    window->menuBar()->hide();
    window->statusBar()->hide();

    shell = new QWidget(window);
    shell->setObjectName("touchShell");
    shell->setStyleSheet(QString::fromLatin1(touchStyle));
    auto *layout = new QVBoxLayout(shell);
    layout->setContentsMargins(6, 4, 6, 4);
    layout->setSpacing(4);
    auto *header = new QHBoxLayout;
    pageTitle = new QLabel(tr("GQRX / TOUCH"), shell);
    pageTitle->setObjectName("pageTitle");
    header->addWidget(pageTitle, 1);
    auto *dsp = window->findChild<QAction *>("actionDSP");
    if (dsp) {
        auto *run = button(dsp->isChecked() ? tr("Stop RX") : tr("Start RX"), shell, "touchRun");
        run->setCheckable(true);
        run->setChecked(dsp->isChecked());
        run->setEnabled(dsp->isEnabled());
        header->addWidget(run);
        connect(run, &QPushButton::clicked, dsp, &QAction::trigger);
        connect(dsp, &QAction::changed, run, [dsp, run] {
            run->setChecked(dsp->isChecked());
            run->setEnabled(dsp->isEnabled());
            run->setText(dsp->isChecked() ? tr("Stop RX") : tr("Start RX"));
        });
    }
    auto *classic = button(tr("Desktop"), shell, "touchDesktop");
    header->addWidget(classic);
    connect(classic, &QPushButton::clicked, this, [this] { setEnabled(false); });
    layout->addLayout(header);

    frequencyButton = button({}, shell, "touchFrequency");
    frequencyButton->setMinimumHeight(48);
    frequencyButton->setAccessibleName(tr("Receive frequency. Activate to enter MHz."));
    connect(frequencyButton, &QPushButton::clicked, this, &TouchController::showKeypad);
    layout->addWidget(frequencyButton);
    refreshFrequency();
    pages = new QStackedWidget(shell);
    pages->setObjectName("touchPages");
    // Hidden large pages must not dictate the minimum size of the handheld shell.
    pages->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    layout->addWidget(pages, 1);
    auto *home = new QWidget(pages);
    auto *homeLayout = new QVBoxLayout(home);
    homeLayout->setContentsMargins(0, 0, 0, 0);
    homeLayout->setSpacing(4);
    auto *quick = new QHBoxLayout;
    if (!panels.isEmpty()) {
        if (auto *mode = panels[0].content->findChild<QComboBox *>("modeSelector"))
            quick->addWidget(mirrorCombo(mode, "touchMode"), 2);
        if (auto *filter = panels[0].content->findChild<QComboBox *>("filterCombo"))
            quick->addWidget(mirrorCombo(filter, "touchFilter"), 1);
    }
    homeLayout->addLayout(quick);
    homeLayout->addWidget(plot, 1);
    plot->show();
    pages->addWidget(home);

    for (auto &panel : panels) {
        panel.content->setParent(shell);
        panel.dock->setWidget(nullptr);
        panel.actionEnabled = panel.dock->toggleViewAction()->isEnabled();
        panel.dock->toggleViewAction()->setEnabled(false);
        panel.dock->hide();
        adaptControls(panel.content);
        panel.scroll = scrollable(panel.content, pages);
        pages->addWidget(panel.scroll);
    }
    // Preserve the original meter, frequency digit editor and marker controls too.
    adaptControls(desktop);
    auto *meters = scrollable(desktop, pages);
    pages->addWidget(meters);

    commands = createCommandsPage();
    pages->addWidget(commands);

    auto *tuning = new QHBoxLayout;
    auto *down = button(tr("− Tune"), shell, "touchTuneDown");
    auto *up = button(tr("Tune +"), shell, "touchTuneUp");
    down->setAutoRepeat(true); up->setAutoRepeat(true);
    step = new QComboBox(shell);
    step->setObjectName("touchStep");
    step->setAccessibleName(tr("Tuning step"));
    const QList<qint64> steps{1, 10, 100, 500, 1000, 2500, 5000, 6250, 8333, 9000, 10000, 12500, 25000, 50000, 100000, 1000000};
    for (auto value : steps) step->addItem(value < 1000 ? tr("%1 Hz").arg(value) : tr("%1 kHz").arg(value / 1000.0), value);
    QSettings settings;
    int saved = step->findData(settings.value("touch/stepHz", 12500).toLongLong());
    step->setCurrentIndex(saved < 0 ? 11 : saved);
    step->setMinimumHeight(controlHeight);
    connect(step, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
        QSettings s; s.setValue("touch/stepHz", step->currentData());
    });
    connect(down, &QPushButton::clicked, this, [this] { frequency->setFrequency(frequency->getFrequency() - step->currentData().toLongLong()); });
    connect(up, &QPushButton::clicked, this, [this] { frequency->setFrequency(frequency->getFrequency() + step->currentData().toLongLong()); });
    tuning->addWidget(down, 1); tuning->addWidget(step, 1); tuning->addWidget(up, 1);
    layout->addLayout(tuning);
    auto *nav = new QHBoxLayout;
    const QStringList names{tr("Tune"), tr("Receiver"), tr("RF input"), tr("Audio"), tr("More")};
    for (int i = 0; i < names.size(); ++i) {
        auto *b = button(names[i], shell, QString("touchNav%1").arg(i));
        nav->addWidget(b, 1);
        connect(b, &QPushButton::clicked, this, [this, i, names] {
            if (i == 0) showPage(0, tr("GQRX / TOUCH"));
            else if (i == 4) showCommands();
            else showPanel(i - 1);
        });
    }
    layout->addLayout(nav);
    hint = new QLabel(tr("Arrows/WASD: move   Enter: use/edit   Esc: back   Tab: next"), shell);
    hint->setObjectName("touchHint");
    layout->addWidget(hint);
    window->setCentralWidget(shell);
    window->setMinimumSize(0, 0);
    window->resize(640, 480);
    frequencyButton->setFocus();
    // Dialogs that were already open when toggling must be usable too.
    for (auto *w : QApplication::topLevelWidgets())
        if (auto *d = qobject_cast<QDialog *>(w)) if (d->isVisible()) adaptDialog(d);
}

QScrollArea *TouchController::createCommandsPage()
{
    const int meterPage = panels.size() + 1;
    auto *more = new QWidget;
    auto *grid = new QGridLayout(more);
    grid->setContentsMargins(0, 0, 0, 0);
    int cell = 0;
    for (int i = 0; i < panels.size(); ++i) {
        auto *b = button(panels[i].dock->windowTitle(), more);
        grid->addWidget(b, cell / 2, cell % 2);
        ++cell;
        connect(b, &QPushButton::clicked, this, [this, i] { showPanel(i); });
    }
    auto *meterButton = button(tr("Meters / markers"), more);
    grid->addWidget(meterButton, cell / 2, cell % 2); ++cell;
    connect(meterButton, &QPushButton::clicked, this, [this, meterPage] { showPage(meterPage, tr("Meters / markers")); });
    // Use the real actions from every menu, including recent configurations.
    // Dock/toolbar layout actions have no meaning while their content is paged.
    QSet<QAction *> seen;
    std::function<void(QMenu *)> addMenu = [&](QMenu *menu) {
        if (!menu || menu->objectName() == "menu_View") return;
        for (auto *action : menu->actions()) {
            if (action->menu()) { addMenu(action->menu()); continue; }
            if (action->isSeparator() || !action->isVisible() || seen.contains(action)) continue;
            seen.insert(action);
            auto *b = button(action->text().remove('&'), more);
            b->setCheckable(action->isCheckable());
            b->setChecked(action->isChecked());
            b->setEnabled(action->isEnabled());
            b->setToolTip(action->toolTip());
            grid->addWidget(b, cell / 2, cell % 2); ++cell;
            connect(b, &QPushButton::clicked, action, &QAction::trigger);
            connect(action, &QAction::changed, b, [action, b] {
                b->setText(action->text().remove('&'));
                b->setEnabled(action->isEnabled());
                b->setChecked(action->isChecked());
            });
        }
    };
    for (auto *action : window->menuBar()->actions()) addMenu(action->menu());
    if (auto *full = window->findChild<QAction *>("actionFullScreen")) {
        auto *b = button(tr("Full screen"), more);
        grid->addWidget(b, cell / 2, cell % 2);
        connect(b, &QPushButton::clicked, full, &QAction::trigger);
    }
    grid->setRowStretch(grid->rowCount(), 1);
    return scrollable(more, pages);
}

void TouchController::showCommands()
{
    // Recent-file actions can be replaced by upstream after loading a configuration.
    pages->removeWidget(commands);
    commands->deleteLater();
    for (int i = controls.size() - 1; i >= 0; --i)
        if (!controls[i].widget) controls.remove(i);
    commands = createCommandsPage();
    pages->addWidget(commands);
    showPage(pages->indexOf(commands), tr("More"));
}

void TouchController::restore()
{
    editing.clear();
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, nativeDialogsDisabled);
    restoreDialogs();
    for (auto &state : auxiliaryWindows) {
        if (!state.window) continue;
        auto *wrapper = state.window->takeCentralWidget();
        state.central->setParent(state.window);
        state.window->setCentralWidget(state.central);
        delete wrapper;
        state.window->setStyleSheet(state.style);
        state.window->setMinimumSize(state.minimum);
        state.window->setMaximumSize(state.maximum);
        state.window->resize(state.size);
    }
    auxiliaryWindows.clear();
    plot->setParent(desktop);
    desktop->layout()->addWidget(plot);
    for (auto &panel : panels) {
        panel.scroll->takeWidget();
        panel.dock->setWidget(panel.content);
        panel.dock->toggleViewAction()->setEnabled(panel.actionEnabled);
        panel.scroll = nullptr;
    }
    if (auto *area = qobject_cast<QScrollArea *>(desktop->parentWidget()->parentWidget())) area->takeWidget();
    desktop->setParent(window);
    window->takeCentralWidget();
    window->setCentralWidget(desktop);
    // deleteLater is necessary when this is called from the Desktop button.
    shell->hide();
    shell->deleteLater();
    shell = nullptr; frequencyButton = nullptr; pages = nullptr; hint = nullptr;
    for (const auto &state : controls) {
        if (!state.widget) continue;
        state.widget->setMinimumSize(state.minimum);
        state.widget->setMaximumSize(state.maximum);
        state.widget->setFocusPolicy(state.focus);
    }
    controls.clear();
    window->restoreGeometry(desktopGeometry);
    window->restoreState(desktopState);
    window->menuBar()->setVisible(menuVisible);
    window->statusBar()->setVisible(statusVisible);
    desktop->show(); plot->show();
    desktop = nullptr;
}

void TouchController::refreshFrequency()
{
    if (frequencyButton)
        frequencyButton->setText(tr("%1 MHz").arg(frequency->getFrequency() / 1e6, 0, 'f', 6));
}

void TouchController::showPanel(int index)
{
    if (enabled && index >= 0 && index < panels.size())
        showPage(index + 1, panels[index].dock->windowTitle());
}

void TouchController::showPage(int index, const QString &title)
{
    editing.clear();
    pages->setCurrentIndex(index);
    pageTitle->setText(title);
    focusFirst(pages->currentWidget());
}

void TouchController::focusFirst(QWidget *page)
{
    for (auto *w : page->findChildren<QWidget *>()) {
        if (w->isVisibleTo(page) && w->isEnabled() && (w->focusPolicy() & Qt::TabFocus)) {
            w->setFocus(Qt::TabFocusReason);
            return;
        }
    }
    frequencyButton->setFocus();
}

void TouchController::showKeypad()
{
    QDialog dialog(window);
    dialog.setObjectName("touchKeypad");
    dialog.setWindowTitle(tr("Tune frequency (MHz)"));
    dialog.setStyleSheet(QString::fromLatin1(touchStyle));
    auto *layout = new QVBoxLayout(&dialog);
    auto *entry = new QLineEdit(QString::number(frequency->getFrequency() / 1e6, 'f', 6), &dialog);
    entry->setObjectName("touchFrequencyEntry");
    entry->setAccessibleName(tr("Frequency in MHz"));
    entry->setMaxLength(20);
    entry->setInputMethodHints(Qt::ImhFormattedNumbersOnly);
    layout->addWidget(entry);
    auto *error = new QLabel(tr("Enter MHz, for example 145.500"), &dialog);
    layout->addWidget(error);
    auto *keys = new QGridLayout;
    const QStringList labels{"1", "2", "3", "4", "5", "6", "7", "8", "9", ".", "0", "⌫"};
    for (int i = 0; i < labels.size(); ++i) {
        const auto label = labels[i];
        auto *b = button(label, &dialog);
        b->setMinimumHeight(44); // Retain generous digit targets on the frequency keypad.
        keys->addWidget(b, i / 3, i % 3);
        connect(b, &QPushButton::clicked, &dialog, [entry, label] {
            if (label == QString::fromUtf8("⌫")) entry->backspace(); else entry->insert(label);
        });
    }
    layout->addLayout(keys);
    auto *row = new QHBoxLayout;
    auto *cancel = button(tr("Cancel"), &dialog);
    auto *clear = button(tr("Clear"), &dialog);
    auto *tune = button(tr("Tune"), &dialog, "touchApplyFrequency");
    row->addWidget(cancel); row->addWidget(clear); row->addWidget(tune);
    layout->addLayout(row);
    connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(clear, &QPushButton::clicked, entry, &QLineEdit::clear);
    auto apply = [this, entry, error, &dialog] {
        bool ok = false;
        const double mhz = entry->text().toDouble(&ok);
        if (!ok || !std::isfinite(mhz) || std::abs(mhz) > 999999.0) {
            error->setText(tr("Enter a valid frequency in MHz."));
            return;
        }
        // The existing frequency controller applies the current device/LNB limits.
        frequency->setFrequency(qRound64(mhz * 1e6));
        dialog.accept();
    };
    connect(tune, &QPushButton::clicked, &dialog, apply);
    connect(entry, &QLineEdit::returnPressed, &dialog, apply);
    dialog.resize(380, 410);
    entry->selectAll();
    entry->setFocus();
    editing = entry;
    dialog.exec();
    editing.clear();
    if (frequencyButton) frequencyButton->setFocus();
}

void TouchController::adaptControls(QWidget *root)
{
    for (auto *w : root->findChildren<QWidget *>()) {
        if (!(qobject_cast<QAbstractButton *>(w) || editor(w))) continue;
        if (qobject_cast<QAbstractSpinBox *>(w->parentWidget())) continue;
        bool already = false;
        for (const auto &state : controls) if (state.widget == w) { already = true; break; }
        if (already) continue;
        controls.append({w, w->minimumSize(), w->maximumSize(), w->focusPolicy()});
        w->setFocusPolicy(Qt::StrongFocus);
        if (qobject_cast<QAbstractItemView *>(w)) continue;
        w->setMaximumHeight(qMax(controlHeight, w->maximumHeight()));
        w->setMinimumHeight(qMax(controlHeight, w->minimumHeight()));
        if (qobject_cast<QAbstractButton *>(w)) {
            w->setMaximumWidth(qMax(controlHeight, w->maximumWidth()));
            w->setMinimumWidth(qMax(controlHeight, w->minimumWidth()));
        }
    }
}

void TouchController::adaptAuxiliaryWindow(QMainWindow *auxiliary)
{
    if (auxiliary == window || !auxiliary->centralWidget()) return;
    for (const auto &state : auxiliaryWindows) if (state.window == auxiliary) return;
    AuxiliaryState state{auxiliary, auxiliary->takeCentralWidget(), auxiliary->minimumSize(),
                         auxiliary->maximumSize(), auxiliary->size(), auxiliary->styleSheet()};
    auto *wrapper = new QWidget;
    auto *layout = new QVBoxLayout(wrapper);
    layout->addWidget(scrollable(state.central, wrapper));
    auto *close = button(tr("Close decoder"), wrapper);
    layout->addWidget(close);
    connect(close, &QPushButton::clicked, auxiliary, &QWidget::close);
    adaptControls(state.central);
    auxiliary->setCentralWidget(wrapper);
    auxiliary->setStyleSheet(QString::fromLatin1(touchStyle));
    auxiliary->setMinimumSize(0, 0);
    auxiliary->setMaximumSize(handheldSize(window));
    auxiliary->resize(600, 430);
    auxiliaryWindows.append(state);
}

void TouchController::adaptDialog(QDialog *dialog)
{
    if (dialog->objectName() == "touchKeypad" || !dialog->layout()) return;
    for (const auto &state : dialogs) if (state.dialog == dialog) return;
    DialogState state{dialog, new QWidget, nullptr, dialog->minimumSize(), dialog->maximumSize(), dialog->size(), dialog->styleSheet()};
    // Transfer the entire original layout; retain every control, connection and button.
    state.content->setLayout(dialog->layout());
    adaptControls(state.content);
    auto *outer = new QVBoxLayout(dialog);
    outer->setContentsMargins(4, 4, 4, 4);
    outer->setSizeConstraint(QLayout::SetNoConstraint);
    state.scroll = scrollable(state.content, dialog);
    outer->addWidget(state.scroll);
    auto *back = button(tr("Back / close"), dialog);
    outer->addWidget(back);
    connect(back, &QPushButton::clicked, dialog, &QDialog::reject);
    dialog->setStyleSheet(QString::fromLatin1(touchStyle));
    dialog->setMinimumSize(0, 0);
    const QSize available = handheldSize(window);
    dialog->setMaximumSize(available);
    dialog->resize(available.width(), qMax(240, available.height() - 36));
    dialogs.append(state);
}

void TouchController::restoreDialogs()
{
    for (auto &state : dialogs) {
        if (!state.dialog) continue;
        auto *dialog = state.dialog.data();
        state.scroll->takeWidget();
        auto *layout = dialog->layout();
        while (auto *item = layout->takeAt(0)) {
            delete item->widget();
            delete item;
        }
        delete layout;
        dialog->setLayout(state.content->layout());
        delete state.content;
        dialog->setStyleSheet(state.style);
        dialog->setMinimumSize(state.minimum);
        dialog->setMaximumSize(state.maximum);
        dialog->resize(state.size);
    }
    dialogs.clear();
}

void TouchController::navigate(QWidget *from, int key)
{
    QWidget *root = from->window();
    if (root == window) root = shell;
    const QPoint origin = from->mapTo(root, from->rect().center());
    QWidget *best = nullptr;
    double score = std::numeric_limits<double>::max();
    for (auto *w : root->findChildren<QWidget *>()) {
        if (w == from || !w->isVisibleTo(root) || !w->isEnabled() || !(w->focusPolicy() & Qt::TabFocus)
            || w->focusProxy() || from->isAncestorOf(w) || w->isAncestorOf(from)) continue;
        // Internal spin-box line edits aren't a separate navigation target.
        if (qobject_cast<QAbstractSpinBox *>(w->parentWidget())) continue;
        const QPoint delta = w->mapTo(root, w->rect().center()) - origin;
        const int primary = key == Qt::Key_Right ? delta.x() : key == Qt::Key_Left ? -delta.x()
                          : key == Qt::Key_Down ? delta.y() : -delta.y();
        const int cross = (key == Qt::Key_Left || key == Qt::Key_Right) ? std::abs(delta.y()) : std::abs(delta.x());
        if (primary <= 0) continue;
        const double candidate = primary + 3.0 * cross;
        if (candidate < score) { score = candidate; best = w; }
    }
    if (best) {
        best->setFocus(Qt::TabFocusReason);
        for (auto *p = best->parentWidget(); p; p = p->parentWidget())
            if (auto *scroll = qobject_cast<QScrollArea *>(p)) scroll->ensureWidgetVisible(best, 12, 12);
    }
}

bool TouchController::eventFilter(QObject *object, QEvent *event)
{
    if (!enabled) return false;
    auto *widget = qobject_cast<QWidget *>(object);
    if (!widget) return false;
    if (event->type() == QEvent::Show) {
        if (auto *dialog = qobject_cast<QDialog *>(widget)) adaptDialog(dialog);
        if (auto *auxiliary = qobject_cast<QMainWindow *>(widget)) adaptAuxiliaryWindow(auxiliary);
        // Device-dependent gain controls can be added after the shell is built.
        if (shell && shell->isAncestorOf(widget)) adaptControls(widget);
    }
    if (event->type() == QEvent::FocusIn) {
        if (editing != widget && (!editing || !editing->isAncestorOf(widget))) editing.clear();
        if (hint) hint->setText(editing ? tr("Editing: arrows adjust   Esc: finish   Tab: next")
                                      : tr("Arrows/WASD: move   Enter: use/edit   Esc: back   Tab: next"));
        for (auto *p = widget->parentWidget(); p; p = p->parentWidget())
            if (auto *scroll = qobject_cast<QScrollArea *>(p)) scroll->ensureWidgetVisible(widget, 12, 12);
    }
    if (event->type() == QEvent::MouseButtonPress && editor(widget)) editing = widget;
    if (event->type() != QEvent::KeyPress && event->type() != QEvent::ShortcutOverride) return false;
    auto *key = static_cast<QKeyEvent *>(event);
    if (QApplication::activePopupWidget()) return false;
    if (widget->window() != window && !qobject_cast<QDialog *>(widget->window())
        && !qobject_cast<QMainWindow *>(widget->window())) return false;
    int navigationKey = key->key();
    if (!editing) {
        switch (navigationKey) {
        case Qt::Key_W: navigationKey = Qt::Key_Up; break;
        case Qt::Key_A: navigationKey = Qt::Key_Left; break;
        case Qt::Key_S: navigationKey = Qt::Key_Down; break;
        case Qt::Key_D: navigationKey = Qt::Key_Right; break;
        default: break;
        }
    }
    const bool arrow = navigationKey >= Qt::Key_Left && navigationKey <= Qt::Key_Down;
    const bool tuneShortcut = key->key() == Qt::Key_F && !editing && widget->window() == window;
    const bool enter = key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter;
    const bool escape = key->key() == Qt::Key_Escape;
    if (key->modifiers() != Qt::NoModifier) return false;
    if (event->type() == QEvent::ShortcutOverride) {
        // Prevent Gqrx's single-letter shortcuts from stealing text entry.
        if (editing || arrow || enter || escape || tuneShortcut) { key->accept(); return true; }
        return false;
    }
    if (tuneShortcut) { showKeypad(); return true; }
    if (escape) {
        if (editing) {
            editing.clear();
            if (hint) hint->setText(tr("Arrows/WASD: move   Enter: use/edit   Esc: back   Tab: next"));
            return true;
        }
        if (widget->window() == window) { showPage(0, tr("GQRX / TOUCH")); return true; }
        return false;
    }
    if (editing && (editing == widget || editing->isAncestorOf(widget))) return false;
    if (arrow) { navigate(widget, navigationKey); return true; }
    if (enter) {
        if (auto *b = qobject_cast<QAbstractButton *>(widget)) { b->click(); return true; }
        if (editor(widget)) {
            editing = widget;
            if (hint) hint->setText(tr("Editing: arrows adjust   Esc: finish   Tab: next"));
            if (auto *combo = qobject_cast<QComboBox *>(widget)) combo->showPopup();
            return true;
        }
    }
    return false;
}
