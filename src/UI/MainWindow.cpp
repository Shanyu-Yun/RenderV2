#include "MainWindow.hpp"
#include "VkRenderWindow.hpp"
#include <QAction>
#include <QMenu>
#include <QMenuBar>
#include <QWidget>
#include <iostream>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), m_vkWindow(nullptr), m_windowContainer(nullptr)
{
    setupUI();
    setupMenuBar();
}

MainWindow::~MainWindow()
{
    std::cout << "MainWindow destroyed" << std::endl;
    // Qt的父子对象系统会自动删除m_vkWindow和m_windowContainer
}

void MainWindow::setupUI()
{
    // 创建Vulkan渲染窗口（QWindow）
    m_vkWindow = new VkRenderWindow();

    // 将QWindow嵌入到QWidget容器中
    m_windowContainer = QWidget::createWindowContainer(m_vkWindow, this);
    m_windowContainer->setMinimumSize(800, 600);

    // 设置为中央窗口部件
    setCentralWidget(m_windowContainer);

    setWindowTitle("QTRender - Vulkan Renderer");
}

void MainWindow::setupMenuBar()
{
    // 创建菜单栏
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));

    QAction *exitAction = new QAction(tr("E&xit"), this);
    exitAction->setShortcut(QKeySequence::Quit);
    connect(exitAction, &QAction::triggered, this, &QWidget::close);
    fileMenu->addAction(exitAction);

    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
    // 未来可以添加视图相关的菜单项
}