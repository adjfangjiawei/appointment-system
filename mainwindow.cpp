#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QFile>
#include <QVector>
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
     islogin=false;
}

MainWindow::~MainWindow()
{
    delete ui;
}


void MainWindow::on_character_clicked()
{
    if(islogin==false){
        ui->informationshow->setText("你还未登录");
        return;
    }
    QString str=ui->charctorcotent->toPlainText();
    if(str=="管理员"){
    ui->informationshow->setText("你选择了管理员");
    characterstate=ADMIN;
    }
    else if(str=="学生"){
    ui->informationshow->setText("你选择了学生");
    characterstate=STUDENT;
    }
}

void MainWindow::on_time_clicked()
{
    if(islogin==false){
        ui->informationshow->setText("你还未登录");
        return;
    }
    QString str=ui->timecontent->toPlainText();
    time=str.toInt();
    ui->informationshow->setText("你选择了"+str+":00-"+QString::number(time+1)+":00");
}

void MainWindow::on_function_clicked()
{
    if(islogin==false){
        ui->informationshow->setText("你还未登录");
        return;
    }
    QString str=ui->functioncontent->toPlainText();
    QFile file("count.txt");
    if(!file.open(QIODevice::ReadOnly | QIODevice::Text)){ui->informationshow->setText("打开文件失败");}
    QVector<lab> thelabs;
    QVector<QString> allmachineline;
    //所有电脑的文本
    while(!file.atEnd()) {
        QByteArray line = file.readLine();
        allmachineline.push_back(line);
        QString comp(line);

        auto splitcontent=comp.split(" ");
        struct lab a;
        a.No=splitcontent[0].toInt();
        a.name=splitcontent[1];
        a.time=splitcontent[2].toInt();

        a.number=splitcontent[3].toInt();
        thelabs.push_back(a);
    }
    //读取状态文件
    QString showstr;
    //显示的预约信息
    //读取电脑信息
    if(str=="查询预约状况"){
        for(auto i:thelabs){
            if(i.name!="???"){
                showstr=QString::number(i.No)+"已被"+"姓名："+i.name+" 学号： "+QString::number(i.number)+"预约"+"\n";

            }
              ui->debug->setText(QString::number(i.time));
        }
        if(showstr==""){
            ui->informationshow->setText("你没有能预约的机器");

        }
        //如果没有这个时间段的机器就输出没有机器
        else{
            ui->informationshow->setText(showstr);
        }

    }
    if(str=="预约"){
        int searchindex=0;
        bool isappointsucceed=true;
        for(auto i=0;i<thelabs.length();i++){
            if(thelabs[i].No==machinenumber&&thelabs[i].time==time){

              ui->informationshow->setText("此机器已被预约");


              isappointsucceed=false;

            }
              //匹配预约机器
        }
        if(isappointsucceed==true){
            ui->informationshow->setText("你已成功预约");
            auto mystring=QString::number(machinenumber)+" "+username+" "+QString::number(time)+" "+QString::number(usernumber)+"\n";
            allmachineline.push_back(mystring);
        }
    }
    if(str=="取消预约"){
        for(auto i=0;i<thelabs.length();i++){
            if(thelabs[i].name==username&&thelabs[i].No==machinenumber&&thelabs[i].time==time){
              ui->informationshow->setText("你已取消预约");
             allmachineline.erase(allmachineline.begin() + i);

            }
              //匹配预约机器
            else if(thelabs[i].name!=username&&thelabs[i].No==machinenumber&&thelabs[i].time==time){
              ui->informationshow->setText("你未预约此机器");
            }
        }

    }
    if(str=="标记故障电脑"){
        if(characterstate==ADMIN){
            QString brokenstring;
            QFile filebroken("error.txt");
            if(!filebroken.open(QIODevice::WriteOnly | QIODevice::Text)){ui->informationshow->setText("打开文件失败");}
            for(auto i=0;i<thelabs.length();i++){
                if(thelabs[i].No==machinenumber){
                brokenstring=brokenstring+QString::number(machinenumber)+" !"+"\n";

                }
            }
            filebroken.write(brokenstring.toUtf8());
            filebroken.close();

        }
        else{
            ui->informationshow->setText("学生不具有标记故障电脑的权限");
        }
        //只有管理员才有标记故障电脑的权限
    }
    if(str=="查询实验情况"){
        if(characterstate==ADMIN){
        QString allstudentsname;
        int studentnumber=0;
        for(auto i=0;i<thelabs.length();i++){
            if(thelabs[i].name!="???"){
            studentnumber++;
            allstudentsname=allstudentsname+" "+thelabs[i].name;
            ui->informationshow->setText("有"+QString::number(studentnumber)+"个学生预约了实验\n分别是"+allstudentsname);

            }
        }
        }
        else{
            ui->informationshow->setText("学生不具有查询实验情况的权限");
        }
    }

    file.close();
    file.flush();
    QFile file1("count.txt");
    file1.open(QIODevice::WriteOnly | QIODevice::Text);
    QString writeall;
    for(auto i:allmachineline){
        writeall=writeall+i;
    }
    file1.write(writeall.toUtf8());
    file1.close();
}

void MainWindow::on_machine_clicked()
{
    if(islogin==false){
        ui->informationshow->setText("你还未登录");
        return;
    }
    QString str=ui->machineinput->toPlainText();
    ui->informationshow->setText("你所选机器号为"+str);
    machinenumber=str.toInt();
}
//选择机器
void MainWindow::on_login_clicked()
{
    bool flag=false;
    username=ui->username->toPlainText();
    usernumber=ui->usernumber->toPlainText().toInt();
    userpassword=ui->password->toPlainText().toInt();
    QFile file("login.txt");
    file.open(QIODevice::ReadWrite|QIODevice::Text);
    QString linestr;
    student onestudent;
    QVector<student> allstu;
    while(!file.atEnd()) {
        linestr=file.readLine();
        auto splitcontent=linestr.split(" ");
        onestudent.name=splitcontent[0];
        onestudent.ID=splitcontent[1].toInt();
        onestudent.password=splitcontent[2].toInt();
        allstu.push_back(onestudent);

    }
    //读取用户信息表
    for(auto i:allstu){
        if(username==i.name&&userpassword==i.password&&usernumber==i.ID){
              ui->informationshow->setText("姓名："+username+"学号："+QString::number(usernumber)+"已登录");
              islogin=true;
              flag=true;
        }
    }
    if(flag==false){
         ui->informationshow->setText("用户不存在或者账号错误");
    }


}
//登录

void MainWindow::on_register_2_clicked()
{
    QFile file("login.txt");
    auto registername=ui->registername->toPlainText();
    auto registerID=ui->reisterID->toPlainText().toInt();
    auto registerpassword=ui->registerpassword->toPlainText().toInt();
    bool flag=false;
    QString linestr;
    //读取代码的零时变量
    QVector<student> allstu;
    student onestudent;
    file.open(QIODevice::ReadWrite|QIODevice::Text);
    while(!file.atEnd()) {
        linestr=file.readLine();
        auto splitcontent=linestr.split(" ");
        onestudent.name=splitcontent[0];
        onestudent.ID=splitcontent[1].toInt();
        onestudent.password=splitcontent[2].toInt();
        allstu.push_back(onestudent);

    }
    for(auto i:allstu){
        if(i.ID==registerID&&i.name==registername){
            ui->informationshow->setText("你注册的用户已存在");
            flag=true;
        }
    }
    if(flag==false){
        file.write((registername+" "+QString::number(registerID)+" "+QString::number(registerpassword)+"\n").toUtf8());
        ui->informationshow->setText("你已注册成功");
    }

    file.close();
}

void MainWindow::on_bytime_clicked()
{
    sub=new SubWindow(this);
    sub->show();

}
