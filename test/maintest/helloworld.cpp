#include <iostream>
#include <string>
#include <string.h>

using std::cout;
using std::endl;


void test(char* a[]){
   cout<<"test"<<endl;
   a[0]="not goo";
   //计算char*到底多长，应该使用strlen方法。
   strlen(a[0]); // 输出7
}


int main(int argc,char* argv[]){  //[]优先结合，因此传入的是一个全是char*的数组。 由于c的特性，实际传入的是首元素的地址。
    //因此我们光依靠argv是无法知道长度的，因此才有的第一个参数。
    //当我们执行./helloworld的时候，argv[0]的输出实际上就是./helloworld,argc==1
    //执行 ./hellowolrd 1234 那么argc就是2，argv[1]就是1234
    cout<<"argc :"<<argc<<endl;//默认是1 
    char* s[10];
    char*(* s_v)[10] = &s; //先用括号结合代表其是一个指针，指向了一个都是char*的长度为10的数组。
    int argv_size  = sizeof(s_v);
    s[0] = "hello world";
    cout<<std::string(s[0])<<endl;
    test(s);
    cout<<std::string(s[0])<<endl;//发生改变，证明传递的是指针
    cout<<std::string(argv[0])<<endl;
    cout<<"argv size is :"<<argv_size<<endl; //sizeof指针只会输出指针大小 64位机器为8
    char* p = "xxxx"; //指向内存常量区
    //p[1]='y'; //这样修改会导致段错误
    //char pstr[] = "hello world."; //可以修改，是堆栈上的内容
    p="xxaa";//这样是改变指向
    cout<<std::string(p)<<endl;
    return 0;
}

//string.c_str()会自动在字符后面补上\0,反之则是自动去掉。