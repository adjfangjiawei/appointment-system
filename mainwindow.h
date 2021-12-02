#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <information.h>
#include <subwindow.h>
#define ADMIN 0
#define STUDENT 1
QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void on_character_clicked();

    void on_time_clicked();

    void on_function_clicked();

    void on_machine_clicked();

    void on_login_clicked();

    void on_register_2_clicked();

    void on_bytime_clicked();

private:
    Ui::MainWindow *ui;
    int characterstate;
    int time;
    int machinenumber;
    QString username;
    int usernumber;
    int userpassword;
    bool islogin;
    SubWindow *sub;
};
#endif // MAINWINDOW_H
