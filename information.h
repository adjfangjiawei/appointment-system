#ifndef INFORMATION_H
#define INFORMATION_H
#include <QString>
struct admin
{
 char name[10]; //姓名
 char password[10]; // 密码
 char ID[5] ; //工号
};
struct student
{
 QString name; //姓名
int password; // 密码
 int ID ; //学号
 int time;
};
struct lab
{
int No;   //编号
QString name; //姓名
int number; //学号
int time; //时间段状态
bool operator==( lab b)
     {
        if(this->No==b.No&&this->name==b.name&&this->number==b.number&&this->time==b.time){
               return true;
        }
        else{
            return false;
        }

     }

};
#endif // INFORMATION_H
