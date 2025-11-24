#include "VkRenderWindow.hpp"
#include <iostream>
VkRenderWindow::VkRenderWindow(QWindow *parent) : QWindow(parent)
{
}

VkRenderWindow::~VkRenderWindow()
{
    std::cout << "VkRenderWindow destroyed" << std::endl;
}