#ifndef QTERMHTTP_H
#define QTERMHTTP_H

#include <qobject.h>
#include <qhttp.h>
#include <qprogressdialog.h>

class QTermHttp : public QObject
{
	Q_OBJECT

public:
	QTermHttp();
	~QTermHttp();

	void getLink(const QString&, bool);
protected slots:
	void cancel();
    void httpDone(bool);
    void httpRead(int,int);
    void httpResponse( const QHttpResponseHeader &);
	void previewImage(const QString&);

signals:
	void done(QTermHttp*);

protected:
    QHttp m_httpDown;
    QString m_strHttpFile;
    QProgressDialog m_pDialog;
    bool m_bPreview;
	bool m_bExist;
};

#endif
