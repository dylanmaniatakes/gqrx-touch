// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>
#include <QTemporaryDir>
#include <QSettings>
#include <QComboBox>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QMenuBar>
#include <QAction>
#include <QDir>
#include <cmath>
#ifdef WITH_PORTAUDIO
#include <portaudio.h>
#endif
#include "applications/gqrx/mainwindow.h"
#include "qtgui/touch/touchcontroller.h"
#include "qtgui/freqctrl.h"

class IntegrationTest : public QObject {
    Q_OBJECT
private slots:
    void realWindow() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        qputenv("XDG_CONFIG_HOME", dir.path().toUtf8());
        QCoreApplication::setOrganizationName("gqrx-touch-integration");
        QCoreApplication::setApplicationName("isolated");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
        QSettings prefs; prefs.setValue("touch/enabled", true);
        QFile iq(dir.filePath("synthetic.cfile"));
        QVERIFY(iq.open(QIODevice::WriteOnly));
        for (int i = 0; i < 192000; ++i) {
            float pair[2] = {float(0.1 * std::cos(i * 2.0 * M_PI * 12500 / 192000)),
                             float(0.1 * std::sin(i * 2.0 * M_PI * 12500 / 192000))};
            iq.write(reinterpret_cast<const char *>(pair), sizeof(pair));
        }
        iq.close();
        const QString config = dir.filePath("receiver.conf");
        QSettings settings(config, QSettings::IniFormat);
        settings.setValue("configversion", 4);
        settings.setValue("input/device", QString("file='%1',rate=192000,freq=145500000,repeat=true,throttle=true").arg(iq.fileName()));
        settings.setValue("input/sample_rate", 192000);
        settings.setValue("input/frequency", 145500000);
        settings.setValue("receiver/demod", "OFF");
        settings.setValue("audio/gain", -80);
        settings.sync();
#ifdef WITH_PORTAUDIO
        QCOMPARE(Pa_Initialize(), paNoError);
#endif
        {
            MainWindow window(config, false);
            QVERIFY(window.configOk);
            window.show();
            auto *touch = window.findChild<TouchController *>();
            QVERIFY(touch); QVERIFY(touch->isEnabled());
            auto *desktop = window.findChild<QWidget *>("centralWidget");
            auto *freq = window.findChild<CFreqCtrl *>("freqCtrl");
            QVERIFY(freq);
            window.resize(640, 480);
            QTest::qWait(100);
            QCOMPARE(window.size(), QSize(640, 480));
            auto *rx = window.findChild<DockRxOpt *>();
            auto *mode = window.findChild<QComboBox *>("touchMode");
            QVERIFY(mode);
            mode->setCurrentIndex(DockRxOpt::MODE_USB); emit mode->activated(DockRxOpt::MODE_USB);
            QCOMPARE(rx->currentDemod(), int(DockRxOpt::MODE_USB));
            mode->setCurrentIndex(DockRxOpt::MODE_OFF); emit mode->activated(DockRxOpt::MODE_OFF);
            auto *run = window.findChild<QPushButton *>("touchRun");
            run->click();
            QVERIFY(window.findChild<QAction *>("actionDSP")->isChecked());
            QTest::qWait(800);
            QString shots = qEnvironmentVariable("GQRX_TOUCH_SCREENSHOTS");
            if (!shots.isEmpty()) {
                QDir().mkpath(shots);
                QVERIFY(window.grab().save(shots + "/touch-tune.png"));
            }
            window.findChild<QPushButton *>("touchTuneUp")->click();
            QCOMPARE(freq->getFrequency(), 145512500LL);
            for (int i = 0; i < 6; ++i) {
                touch->showPanel(i);
                QCoreApplication::processEvents();
                QCOMPARE(window.size(), QSize(640, 480));
                auto *pages = window.findChild<QStackedWidget *>("touchPages");
                auto *scroll = qobject_cast<QScrollArea *>(pages->currentWidget());
                QVERIFY(scroll);
                QVERIFY(scroll->widget());
                if (!shots.isEmpty()) QVERIFY(window.grab().save(shots + QString("/touch-panel-%1.png").arg(i)));
            }
            window.findChild<QPushButton *>("touchNav4")->click();
            QCoreApplication::processEvents();
            if (!shots.isEmpty()) QVERIFY(window.grab().save(shots + "/touch-more.png"));
            // Actual upstream dialogs retain their fields and fit within the screen.
            for (const QString name : {"actionIqTool", "actionRemoteConfig"}) {
                auto *action = window.findChild<QAction *>(name);
                QVERIFY(action);
                bool inspected = false;
                QTimer::singleShot(0, &window, [&] {
                    for (auto *w : QApplication::topLevelWidgets()) {
                        auto *dialog = qobject_cast<QDialog *>(w);
                        if (!dialog || !dialog->isVisible()) continue;
                        QVERIFY(dialog->width() <= 640);
                        QVERIFY(dialog->height() <= 480);
                        inspected = true;
                        dialog->reject();
                    }
                });
                action->trigger();
                QTest::qWait(40);
                QVERIFY(inspected);
            }
            run->click();
            QVERIFY(!window.findChild<QAction *>("actionDSP")->isChecked());
            for (int i = 0; i < 3; ++i) {
                touch->setEnabled(false);
                QCOMPARE(window.centralWidget(), desktop);
                QCOMPARE(rx->widget()->findChild<QComboBox *>("modeSelector")->currentIndex(), int(DockRxOpt::MODE_OFF));
                touch->setEnabled(true);
                QCoreApplication::processEvents();
                QCOMPARE(window.size(), QSize(640, 480));
            }
            window.storeSession();
            QSettings saved(config, QSettings::IniFormat);
            QCOMPARE(saved.value("input/frequency").toLongLong(), freq->getFrequency());
        }
#ifdef WITH_PORTAUDIO
        Pa_Terminate();
#endif
    }
};
QTEST_MAIN(IntegrationTest)
#include "integration_test.moc"
