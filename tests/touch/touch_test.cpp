// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>
#include <QMainWindow>
#include <QDockWidget>
#include <QVBoxLayout>
#include <QComboBox>
#include <QPushButton>
#include <QDialog>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QScrollArea>
#include <QStackedWidget>
#include <QMenuBar>
#include <QMenu>
#include <QToolBar>
#include <QSettings>
#include <QTemporaryDir>
#include "qtgui/freqctrl.h"
#include "qtgui/touch/touchcontroller.h"

class TouchTest : public QObject {
    Q_OBJECT
    QTemporaryDir config;
    QMainWindow *window;
    QWidget *desktop, *plot;
    CFreqCtrl *frequency;
    QList<QDockWidget *> docks;
    TouchController *controller;
    QComboBox *mode;
private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName("gqrx-touch-tests");
        QCoreApplication::setApplicationName("isolated");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, config.path());
    }
    void init() {
        window = new QMainWindow;
        desktop = new QWidget;
        auto *layout = new QVBoxLayout(desktop);
        frequency = new CFreqCtrl;
        frequency->setup(0, 0, 6000000000LL, 1, FCTL_UNIT_NONE);
        frequency->setFrequency(145500000);
        plot = new QWidget;
        plot->setObjectName("fixtureSpectrum");
        layout->addWidget(frequency);
        layout->addWidget(plot);
        window->setCentralWidget(desktop);
        auto *file = window->menuBar()->addMenu("File");
        auto *run = file->addAction("Start DSP");
        run->setObjectName("actionDSP"); run->setCheckable(true);
        auto *bar = window->addToolBar("Main");
        bar->setObjectName("mainToolbar"); bar->addAction(run);
        for (int i = 0; i < 6; ++i) {
            auto *dock = new QDockWidget(QString("Panel %1").arg(i), window);
            dock->setObjectName(QString("dock%1").arg(i));
            auto *body = new QWidget;
            auto *v = new QVBoxLayout(body);
            if (i == 0) {
                mode = new QComboBox;
                mode->setObjectName("modeSelector"); mode->addItems({"AM", "FM", "USB"});
                v->addWidget(mode);
                auto *spin = new QDoubleSpinBox;
                spin->setObjectName("fixtureSpin"); spin->setValue(10); v->addWidget(spin);
            }
            for (int j = 0; j < (i == 3 ? 16 : 2); ++j) v->addWidget(new QPushButton(QString("Option %1").arg(j)));
            dock->setWidget(body);
            window->addDockWidget(Qt::RightDockWidgetArea, dock);
            if (!docks.isEmpty()) window->tabifyDockWidget(docks.last(), dock);
            docks.append(dock);
        }
        window->resize(760, 560);
        window->show();
        controller = new TouchController(window, frequency, plot, docks);
        QCoreApplication::processEvents();
    }
    void cleanup() {
        controller->prepareForShutdown();
        delete controller;
        delete window;
        docks.clear();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    void layoutAndRoundTrip() {
        const QSize size = window->size();
        for (int cycle = 0; cycle < 4; ++cycle) {
            controller->setEnabled(true);
            window->resize(640, 480);
            QCoreApplication::processEvents();
            QCOMPARE(window->size(), QSize(640, 480));
            auto *shell = window->centralWidget();
            for (const auto &name : {"touchFrequency", "touchTuneDown", "touchTuneUp", "touchNav4"}) {
                auto *b = shell->findChild<QPushButton *>(name);
                QVERIFY(b);
                QVERIFY(b->height() >= 38);
                QVERIFY(shell->rect().contains(QRect(b->mapTo(shell, QPoint(0,0)), b->size())));
            }
            QVERIFY(plot->isVisible());
            QVERIFY(plot->height() >= 100);
            for (int i = 0; i < docks.size(); ++i) {
                controller->showPanel(i);
                QVERIFY(!docks[i]->isVisible());
                QVERIFY(!docks[i]->toggleViewAction()->isEnabled());
            }
            controller->setEnabled(false);
            QCoreApplication::processEvents();
            QCOMPARE(window->centralWidget(), desktop);
            QCOMPARE(plot->parentWidget(), desktop);
            for (auto *dock : docks) {
                QCOMPARE(window->dockWidgetArea(dock), Qt::RightDockWidgetArea);
                QVERIFY(!dock->isHidden());
                QVERIFY(dock->toggleViewAction()->isEnabled());
            }
            QCOMPARE(window->size(), size);
            for (auto *dock : docks) QVERIFY(dock->widget());
        }
    }
    void tuningAndModeStayConnected() {
        controller->setEnabled(true);
        QSignalSpy freqSpy(frequency, &CFreqCtrl::newFrequency);
        auto *shell = window->centralWidget();
        shell->findChild<QPushButton *>("touchTuneUp")->click();
        QCOMPARE(frequency->getFrequency(), 145512500LL);
        QVERIFY(freqSpy.count() > 0);
        frequency->setFrequency(100000000);
        QVERIFY(shell->findChild<QPushButton *>("touchFrequency")->text().contains("100.000000"));
        auto *copy = shell->findChild<QComboBox *>("touchMode");
        QSignalSpy modeSpy(mode, QOverload<int>::of(&QComboBox::activated));
        copy->setCurrentIndex(2); emit copy->activated(2);
        QCOMPARE(mode->currentIndex(), 2);
        QCOMPARE(modeSpy.count(), 1);
        mode->setCurrentIndex(1);
        QCOMPARE(copy->currentIndex(), 1);
        frequency->setFrequency(6000000000LL);
        shell->findChild<QPushButton *>("touchTuneUp")->click();
        QCOMPARE(frequency->getFrequency(), 6000000000LL);
    }
    void keyboardNavigateAndEdit() {
        controller->setEnabled(true);
        QCoreApplication::processEvents();
        auto *shell = window->centralWidget();
        auto *down = shell->findChild<QPushButton *>("touchTuneDown");
        auto *step = shell->findChild<QComboBox *>("touchStep");
        down->setFocus();
        QTest::keyClick(down, Qt::Key_Right);
        QCOMPARE(QApplication::focusWidget(), step);
        controller->showPanel(0);
        auto *spin = shell->findChild<QDoubleSpinBox *>("fixtureSpin");
        spin->setFocus();
        QTest::keyClick(spin, Qt::Key_Return);
        QTest::keyClick(spin, Qt::Key_Up);
        QCOMPARE(spin->value(), 11.0);
        QTest::keyClick(spin, Qt::Key_Escape);
        QTest::keyClick(spin, Qt::Key_Up);
        QCOMPARE(spin->value(), 11.0);
        QVERIFY(QApplication::focusWidget() != spin);
    }
    void wasdNavigationAndLiveCommands() {
        controller->setEnabled(true);
        QCoreApplication::processEvents();
        auto *shell = window->centralWidget();
        auto *down = shell->findChild<QPushButton *>("touchTuneDown");
        down->setFocus();
        QTest::keyClick(down, Qt::Key_D);
        QCOMPARE(QApplication::focusWidget(), shell->findChild<QComboBox *>("touchStep"));
        auto *file = window->menuBar()->actions().first()->menu();
        auto *late = file->addAction("New recent configuration");
        QSignalSpy triggered(late, &QAction::triggered);
        shell->findChild<QPushButton *>("touchNav4")->click();
        bool found = false;
        for (auto *b : shell->findChildren<QPushButton *>()) {
            if (b->text() == "New recent configuration") { b->click(); found = true; break; }
        }
        QVERIFY(found);
        QCOMPARE(triggered.count(), 1);
    }
    void controlsRestoreAndAuxiliaryWindow() {
        auto *control = docks.first()->widget()->findChild<QPushButton *>();
        control->setFixedSize(20, 20);
        control->setFocusPolicy(Qt::NoFocus);
        QMainWindow auxiliary(window);
        auto *content = new QWidget;
        auto *layout = new QVBoxLayout(content);
        layout->addWidget(new QPushButton("Decoder option"));
        auxiliary.setCentralWidget(content);
        auxiliary.setMinimumSize(850, 700);
        controller->setEnabled(true);
        auxiliary.show();
        QCoreApplication::processEvents();
        QVERIFY(control->minimumHeight() >= 38);
        QVERIFY(control->focusPolicy() & Qt::TabFocus);
        QVERIFY(auxiliary.width() <= 640);
        QVERIFY(auxiliary.height() <= 480);
        controller->setEnabled(false);
        QCOMPARE(control->minimumSize(), QSize(20, 20));
        QCOMPARE(control->maximumSize(), QSize(20, 20));
        QCOMPARE(control->focusPolicy(), Qt::NoFocus);
        QCOMPARE(auxiliary.centralWidget(), content);
        QCOMPARE(auxiliary.minimumSize(), QSize(850, 700));
    }
    void keypadCommitsThroughOriginalController() {
        controller->setEnabled(true);
        QTimer::singleShot(0, this, [this] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            auto *entry = dialog->findChild<QLineEdit *>("touchFrequencyEntry");
            entry->setText("433.920000");
            dialog->findChild<QPushButton *>("touchApplyFrequency")->click();
        });
        window->centralWidget()->findChild<QPushButton *>("touchFrequency")->click();
        QCOMPARE(frequency->getFrequency(), 433920000LL);
    }
    void dialogsFitAndRestore() {
        QDialog dialog(window);
        auto *layout = new QVBoxLayout(&dialog);
        for (int i = 0; i < 18; ++i) layout->addWidget(new QPushButton(QString::number(i)));
        dialog.setMinimumSize(850, 900);
        const auto minimum = dialog.minimumSize();
        controller->setEnabled(true);
        dialog.show();
        QCoreApplication::processEvents();
        QVERIFY(dialog.width() <= 640);
        QVERIFY(dialog.height() <= 480);
        QCOMPARE(dialog.findChildren<QPushButton *>().size(), 19);
        QVERIFY(dialog.findChild<QScrollArea *>());
        controller->setEnabled(false);
        QCOMPARE(dialog.minimumSize(), minimum);
        QCOMPARE(dialog.findChildren<QPushButton *>().size(), 18);
        QCOMPARE(dialog.layout(), layout);
    }
    void shutdownKeepsTouchPreference() {
        controller->setEnabled(true);
        controller->prepareForShutdown();
        QSettings settings;
        QVERIFY(settings.value("touch/enabled").toBool());
        QCOMPARE(window->centralWidget(), desktop);
    }
};
QTEST_MAIN(TouchTest)
#include "touch_test.moc"
