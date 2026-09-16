// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QPointer>
#include <QVector>
#include <QByteArray>
#include <QSize>

class QAction;
class QMainWindow;
class QDockWidget;
class QStackedWidget;
class QPushButton;
class QLabel;
class QComboBox;
class QDialog;
class QScrollArea;
class CFreqCtrl;

// Presentation-only adapter. Owns no receiver, DSP state or duplicate settings.
class TouchController : public QObject
{
    Q_OBJECT
public:
    TouchController(QMainWindow *window, CFreqCtrl *frequency, QWidget *plot,
                    const QList<QDockWidget *> &panels);
    ~TouchController() override;
    bool isEnabled() const { return enabled; }
    QAction *toggleAction() const { return toggle; }
    void setEnabled(bool on);
    void prepareForShutdown();
    void rememberDesktopLayout(const QByteArray &geometry, const QByteArray &state);
    void showPanel(int index);
    static bool startupEnabled();

protected:
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    struct Panel { QDockWidget *dock; QWidget *content; QScrollArea *scroll = nullptr; bool actionEnabled = true; };
    struct DialogState {
        QPointer<QDialog> dialog;
        QWidget *content;
        QScrollArea *scroll;
        QSize minimum, maximum, size;
        QString style;
    };
    void build();
    QScrollArea *createCommandsPage();
    void showCommands();
    void restore();
    void showPage(int index, const QString &title);
    void showKeypad();
    void adaptDialog(QDialog *dialog);
    void adaptControls(QWidget *root);
    void adaptAuxiliaryWindow(QMainWindow *auxiliary);
    void restoreDialogs();
    void navigate(QWidget *from, int key);
    void focusFirst(QWidget *page);
    void refreshFrequency();
    QComboBox *mirrorCombo(QComboBox *source, const QString &name);

    QMainWindow *window;
    CFreqCtrl *frequency;
    QWidget *plot;
    QVector<Panel> panels;
    QAction *toggle;
    QWidget *desktop = nullptr;
    QWidget *shell = nullptr;
    QStackedWidget *pages = nullptr;
    QScrollArea *commands = nullptr;
    QLabel *pageTitle = nullptr;
    QLabel *hint = nullptr;
    QPushButton *frequencyButton = nullptr;
    QComboBox *step = nullptr;
    QPointer<QWidget> editing;
    QByteArray desktopState, desktopGeometry;
    bool menuVisible = true, statusVisible = true;
    bool enabled = false;
    bool nativeDialogsDisabled = false;
    QVector<DialogState> dialogs;
    struct ControlState { QPointer<QWidget> widget; QSize minimum, maximum; Qt::FocusPolicy focus; };
    QVector<ControlState> controls;
    struct AuxiliaryState { QPointer<QMainWindow> window; QWidget *central; QSize minimum, maximum, size; QString style; };
    QVector<AuxiliaryState> auxiliaryWindows;
};
