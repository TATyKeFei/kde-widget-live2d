#pragma once

#include <QImage>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QTemporaryFile>
#include <memory>

// All methods and resources belong to the transport thread. The GUI sends one
// request at a time and receives an immutable, independently owned image.
class RenderWorker final : public QObject
{
    Q_OBJECT
  public:
    ~RenderWorker() override;
    void start(quint64 generation, const QString &backend);
    void stop(quint64 generation);
    void request(quint64 generation, QJsonObject request);

  Q_SIGNALS:
    void started(quint64 generation);
    void failed(quint64 generation, const QString &error);
    void frameReady(quint64 generation, const QImage &frame, const QJsonObject &header);

  private:
    void readFrames();
    void fail(const QString &error);
    void clearProcess();
    std::unique_ptr<QProcess> m_process;
    std::unique_ptr<QTemporaryFile> m_sharedFrame;
    uchar *m_sharedPixels = nullptr;
    quint64 m_generation = 0;
    QString m_backend;
    QByteArray m_input;
    QJsonObject m_header;
    int m_expectedBytes = -1;
    bool m_inFlight = false, m_trace = false;
    qint64 m_firstByte = 0;
    int m_readCount = 0;
};
