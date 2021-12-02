#ifndef SUBWINDOW_H
#define SUBWINDOW_H

#include <QMainWindow>
#include <QVector>
#include <information.h>
namespace Ui {
class SubWindow;
}

class SubWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit SubWindow(QWidget *parent = nullptr);
    ~SubWindow();

private slots:
    void on_function_clicked();

private:
    Ui::SubWindow *ui;
    QVector<lab> *computeinformation;
    QVector<bool> *isbroken;
    //机器是否故障
};

#endif // SUBWINDOW_H
