#include <iostream>

class Base {
public:
    ~Base() {}  // Невиртуальный деструктор — утечка при полиморфном удалении
};

class Derived : public Base {
public:
    Derived() { data = new int[1000]; }  // Выделение памяти
    ~Derived() { delete[] data; }  // Освобождение в деструкторе наследника
private:
    int* data;
};

int main() {
    Base* obj = new Derived();
    delete obj;  // Утечка, т.к. вызывается деструктор Base, а не Derived
    return 0;
}