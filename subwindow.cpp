#include "subwindow.h"
#include "ui_subwindow.h"
#include <QFile>
SubWindow::SubWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::SubWindow)
{
    ui->setupUi(this);


}

SubWindow::~SubWindow()
{
    delete ui;
}

void SubWindow::on_function_clicked()
{
    auto time=ui->time->toPlainText().toInt();
    isbroken=new QVector<bool>;
    computeinformation=new QVector<lab>;
    bool flag=false;
    for(int i=0;i<12;i++){
        isbroken->push_back(false);

    }
    QFile file("count.txt");
    file.open(QIODevice::ReadOnly | QIODevice::Text);
    QString info;
    //所有电脑的文本
    while(!file.atEnd()) {
        QByteArray line = file.readLine();

        QString comp(line);

        auto splitcontent=comp.split(" ");
        struct lab a;
        a.No=splitcontent[0].toInt();
        a.name=splitcontent[1];
        a.time=splitcontent[2].toInt();

        a.number=splitcontent[3].toInt();
        computeinformation->push_back(a);

    }
    QFile file1("error.txt");
    file1.open(QIODevice::ReadOnly | QIODevice::Text);

    //所有电脑的文本
    while(!file1.atEnd()) {
        QByteArray line = file1.readLine();

        QString comp(line);
        auto splitcontent=comp.split(" ");
        (*isbroken)[splitcontent[0].toInt()]=true;
    }
    for(auto i=0;i<12;i++){

        flag=false;
        if((*isbroken)[i]==true){
            info=info+QString::number(i)+"!"+" ";

        }
        else{
            for(auto j:(*computeinformation)){
            if(j.No==i&&j.time==time){
                info=info+"* ";
                flag=true;
            }
            }
        if(flag==false){
            info=info+"# ";
        }
        }
        if((i+1)%4==0&&i!=0){
            info=info+"\n";
        }

    }
    ui->label->setText(info);
}
