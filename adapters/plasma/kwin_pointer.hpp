#pragma once

#include <QDBusContext>
#include <QObject>
#include <QPointF>
#include <QString>
#include <optional>

namespace nekomote::plasma
{
class KWinPointer final : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "uk.nekopara.NekoMote.Gaze")

  public:
    explicit KWinPointer(QObject* parent);
    ~KWinPointer() override;

    void subscribe();
    void unsubscribe();
    std::optional<QPointF> position() const
    {
        return position_;
    }

  public Q_SLOTS:
    Q_SCRIPTABLE void Update(int x, int y);

  private:
    void start();
    void unload();
    void synchronize();

    QString owner_;
    QString script_path_;
    std::optional<QPointF> position_;
    unsigned users_{0};
    unsigned generation_{0};
    bool registered_{false};
    bool pending_{false};
    bool loaded_{false};
    bool start_requested_{false};
};
} // namespace nekomote::plasma
