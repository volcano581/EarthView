#pragma once
#ifndef MAPVIEWWINDOW_H
#define MAPVIEWWINDOW_H

#include <QMainWindow>
#include <QString>

class Camera;
class MapControlsModel;
class QLabel;
class MapWidget;
class QCloseEvent;
class QShowEvent;

class MapViewWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MapViewWindow(const QString& title, QWidget* parent = nullptr);

    MapWidget* mapWidget() const { return m_mapWidget; }
    /// On-map controls of this window's map; nullptr when built without them.
    MapControlsModel* mapControls() const { return m_mapControls; }

signals:
    void windowVisibilityChanged(bool visible);

protected:
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void setupToolbar();
    void createStatusBar();
    void updateStatus();

private:
    MapWidget* m_mapWidget;
    MapControlsModel* m_mapControls;
    QLabel* m_coordLabel;
    QLabel* m_zoomLabel;
};

#endif // MAPVIEWWINDOW_H
