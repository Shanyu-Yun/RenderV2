#include <QWindow>

class VkRenderWindow : public QWindow
{
    Q_OBJECT
  public:
    explicit VkRenderWindow(QWindow *parent = nullptr);
    ~VkRenderWindow() override;
};