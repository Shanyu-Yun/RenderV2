#pragma once
#include <QMainWindow>

class VkRenderWindow;
class QWidget;

/**
 * @brief 主窗口类
 *
 * 负责管理应用程序的主界面，包含菜单栏、工具栏和Vulkan渲染窗口
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

  public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    VkRenderWindow *getVkRenderWindowHandle() const
    {
        return m_vkWindow;
    }

  private:
    void setupUI();
    void setupMenuBar();

    VkRenderWindow *m_vkWindow; // Vulkan渲染窗口
    QWidget *m_windowContainer; // QWindow的容器Widget
};