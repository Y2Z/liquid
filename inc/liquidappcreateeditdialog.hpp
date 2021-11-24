#pragma once

#include <QAction>
#include <QColorDialog>
#include <QDialog>
#include <QGridLayout>
#include <QLabel>
#include <QListView>
#include <QPlainTextEdit>
#include <QTabWidget>
#include <QtGui>
#include <QtWidgets>

class LiquidAppCreateEditDialog : public QDialog
{
public:
    LiquidAppCreateEditDialog(QWidget* parent = Q_NULLPTR, QString liquidAppName = "");
    ~LiquidAppCreateEditDialog(void);

    QString getName(void);
    bool isPlanningToRun(void);
    void setPlanningToRun(const bool maybe);

public slots:
    void save();

private:
    void bindShortcuts(void);
    static QString colorToRgba(const QColor* color);
    static QFrame* separator(void);

    bool isEditingExisting = false;

    QAction* quitAction;

    QColor backgroundColor;

    QLineEdit* nameInput;
    QLineEdit* addressInput;
    QCheckBox* createIconCheckBox;
    QCheckBox* planningToRunCheckBox;

    // General tab
    QVBoxLayout* generalTabWidgetLayout;
    QLineEdit* titleInput;
    QListView* additionalDomainsListView;
    QStandardItemModel* additionalDomainsModel;
    QLineEdit* userAgentInput;
    QPlainTextEdit* notesTextArea;

    // Appearance tab
    QWidget* appearanceTabWidget;
    QVBoxLayout* appearanceTabWidgetLayout;
    QCheckBox* hideScrollBarsCheckBox;
    QCheckBox* removeWindowFrameCheckBox;
    QCheckBox* useCustomBackgroundCheckBox;
    QPushButton* customBackgroundColorButton;
    QPlainTextEdit* additionalCssTextArea;

    // JavaScript tab
    QWidget* jsTabWidget;
    QVBoxLayout* jsTabWidgetLayout;
    QCheckBox* enableJavaScriptCheckBox;
    QLabel* additionalJsLabel;
    QPlainTextEdit* additionalJsTextArea;

    // Cookies tab
    QWidget* cookiesTabWidget;
    QVBoxLayout* cookiesTabWidgetLayout;
    QCheckBox* allowCookiesCheckBox;
    QCheckBox* allowThirdPartyCookiesCheckBox;

    // Network tab
    QWidget* networkTabWidget;
    QVBoxLayout* networkTabWidgetLayout;
    QRadioButton* proxyModeSystemRadioButton;
    QRadioButton* proxyModeDirectRadioButton;
    QRadioButton* proxyModeCustomRadioButton;
    QComboBox* useSocksSelectBox;
    QLineEdit* proxyHostInput;
    QSpinBox* proxyPortInput;
    QCheckBox* proxyUseAuthCheckBox;
    QLineEdit* proxyUsernameInput;
    QLineEdit* proxyPasswordInput;
};
