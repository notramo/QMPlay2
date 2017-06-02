/*
	QMPlay2 is a video and audio player.
	Copyright (C) 2010-2017  Błażej Szczygieł

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU Lesser General Public License as published
	by the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU Lesser General Public License for more details.

	You should have received a copy of the GNU Lesser General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <Downloader.hpp>

#include <Functions.hpp>
#include <Settings.hpp>
#include <MkvMuxer.hpp>
#include <Demuxer.hpp>
#include <Packet.hpp>
#include <Reader.hpp>

#include <QTimer>
#include <QLabel>
#include <QAction>
#include <QMimeData>
#include <QClipboard>
#include <QFileDialog>
#include <QTreeWidget>
#include <QToolButton>
#include <QGridLayout>
#include <QTreeWidget>
#include <QMessageBox>
#include <QInputDialog>
#include <QProgressBar>
#include <QApplication>
#include <QElapsedTimer>
#if QT_VERSION < 0x050000
	#include <QDesktopServices>
#else
	#include <QStandardPaths>
#endif

#include <functional>

/**/

DownloadItemW::DownloadItemW(DownloaderThread *downloaderThr, QString name, const QIcon &icon, QDataStream *stream) :
	dontDeleteDownloadThr(false), downloaderThr(downloaderThr), finished(false), readyToPlay(false)
{
	QString sizeLText;

	if (stream)
	{
		quint8 type;
		*stream >> filePath >> type >> name;
		finished = true;
		switch (type)
		{
			case 1:
				readyToPlay = true;
				sizeLText = tr("Download complete");
				break;
			case 2:
				sizeLText = tr("Download aborted");
				break;
			case 3:
				sizeLText = tr("Download error");
				break;
		}
	}
	else
		sizeLText = tr("Waiting for connection");

	titleL = new QLabel(name);

	sizeL = new QLabel(sizeLText);

	iconL = new QLabel;
	iconL->setSizePolicy(QSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred));
	iconL->setPixmap(Functions::getPixmapFromIcon(icon.isNull() ? QMPlay2Core.getIconFromTheme("applications-multimedia") : icon, QSize(22, 22), this));

	ssB = new QToolButton;
	if (readyToPlay)
	{
		ssB->setIcon(QMPlay2Core.getIconFromTheme("media-playback-start"));
		ssB->setToolTip(tr("Play"));
	}
	else if (finished)
	{
		ssB->setIcon(QMPlay2Core.getIconFromTheme("view-refresh"));
		ssB->setToolTip(tr("Download again"));
	}
	else
	{
		ssB->setIcon(QMPlay2Core.getIconFromTheme("media-playback-stop"));
		ssB->setToolTip(tr("Stop downloading"));
	}
	connect(ssB, SIGNAL(clicked()), this, SLOT(toggleStartStop()));

	QGridLayout *layout = new QGridLayout(this);
	layout->addWidget(iconL, 0, 0, 2, 1);
	layout->addWidget(titleL, 0, 1, 1, 2);
	layout->addWidget(sizeL, 1, 1, 1, 2);
	if (!finished)
	{
		QHBoxLayout *hLayout = new QHBoxLayout;

		speedProgressW = new SpeedProgressWidget;
		speedProgressW->setLayout(hLayout);

		speedProgressW->progressB = new QProgressBar;
		speedProgressW->progressB->setRange(0, 0);
		hLayout->addWidget(speedProgressW->progressB);

		speedProgressW->speedL = new QLabel;
		hLayout->addWidget(speedProgressW->speedL);

		layout->addWidget(speedProgressW, 2, 0, 1, 2);
	}
	layout->addWidget(ssB, 2, 2, 1, 1);
}
DownloadItemW::~DownloadItemW()
{
	if (!dontDeleteDownloadThr)
	{
		finish(false);
		delete downloaderThr;
	}
}

void DownloadItemW::setName(const QString &name)
{
	if (!finished)
		titleL->setText(name);
}
void DownloadItemW::setSizeAndFilePath(qint64 size, const QString &filePath)
{
	if (!finished)
	{
		sizeL->setText(tr("Size") + ": " + (size > -1 ? Functions::sizeString(size) : "?"));
		speedProgressW->progressB->setRange(0, (size != -1) ? 100 : 0);
		this->filePath = filePath;
	}
}
void DownloadItemW::setPos(int pos)
{
	if (!finished)
		speedProgressW->progressB->setValue(pos);
}
void DownloadItemW::setSpeed(int Bps)
{
	if (!finished)
		speedProgressW->speedL->setText(Functions::sizeString(Bps) + "/s");
}
void DownloadItemW::finish(bool f)
{
	if (!finished)
	{
		delete speedProgressW;
		if (f)
			sizeL->setText(tr("Download complete"));
		else
			sizeL->setText(tr("Download aborted"));
		downloadStop(f);
	}
}
void DownloadItemW::error()
{
	if (!finished)
	{
		if (speedProgressW->progressB->minimum() == speedProgressW->progressB->maximum())
			speedProgressW->progressB->setRange(-1, 0);
		speedProgressW->setEnabled(false);
		sizeL->setText(tr("Download error"));
		downloadStop(false);
	}
}

void DownloadItemW::write(QDataStream &stream)
{
	downloaderThr->write(stream);
	quint8 type = readyToPlay;
	if (!type)
	{
		if (sizeL->text() == tr("Download error"))
			type = 3;
		else
			type = 2;
	}
	stream << filePath << type << titleL->text();
}

void DownloadItemW::toggleStartStop()
{
	if (readyToPlay)
	{
		if (!filePath.isEmpty())
			emit QMPlay2Core.processParam("open", filePath);
		return;
	}
	if (finished)
	{
		filePath.clear();
		emit start();
	}
	else
	{
		finish(false);
		ssB->setEnabled(false);
		emit stop();
	}
}

void DownloadItemW::downloadStop(bool ok)
{
	if (!ok)
	{
		ssB->setIcon(QMPlay2Core.getIconFromTheme("view-refresh"));
		ssB->setToolTip(tr("Download again"));
	}
	else
	{
		ssB->setIcon(QMPlay2Core.getIconFromTheme("media-playback-start"));
		ssB->setToolTip(tr("Play"));
		readyToPlay = true;
	}
	finished = true;
	if (!dontDeleteDownloadThr && visibleRegion() == QRegion())
		emit QMPlay2Core.sendMessage(titleL->text(), sizeL->text());
}

/**/

DownloaderThread::DownloaderThread(QDataStream *stream, const QString &url, DownloadListW *downloadLW, const QString &name, const QString &prefix, const QString &param) :
	url(url), name(name), prefix(prefix), param(param), downloadItemW(nullptr), downloadLW(downloadLW), item(nullptr)
{
	connect(this, SIGNAL(listSig(int, qint64, const QString &)), this, SLOT(listSlot(int, qint64, const QString &)));
	connect(this, SIGNAL(finished()), this, SLOT(finished()));
	if (stream)
	{
		*stream >> this->url >> this->prefix >> this->param;
		item = new QTreeWidgetItem(downloadLW);
		downloadLW->setItemWidget(item, 0, (downloadItemW = new DownloadItemW(this, QString(), getIcon(), stream)));
		connect(downloadItemW, SIGNAL(start()), this, SLOT(start()));
		connect(downloadItemW, SIGNAL(stop()), this, SLOT(stop()));
	}
	else
		start();
}
DownloaderThread::~DownloaderThread()
{
	disconnect(this, SIGNAL(finished()), this, SLOT(finished()));
	stop();
	if (!wait(2000))
	{
		terminate();
		wait(500);
	}
}

void DownloaderThread::listSlot(int param, qint64 val, const QString &filePath)
{
	switch (param)
	{
		case ADD_ENTRY:
			if (!item)
				item = new QTreeWidgetItem(downloadLW);
			if (downloadItemW)
			{
				downloadItemW->dontDeleteDownloadThr = true;
				downloadItemW->deleteLater();
			}
			downloadLW->setItemWidget(item, 0, (downloadItemW = new DownloadItemW(this, name.isEmpty() ? url : name, getIcon())));
			connect(downloadItemW, SIGNAL(start()), this, SLOT(start()));
			connect(downloadItemW, SIGNAL(stop()), this, SLOT(stop()));

			// Workaround: Resize the widget twice to get correct item size
			downloadLW->resize(downloadLW->size() + QSize(0, 1));
			downloadLW->resize(downloadLW->size() - QSize(0, 1));

			break;
		case NAME:
			downloadItemW->setName(name);
			break;
		case SET:
			downloadItemW->setSizeAndFilePath(val, filePath);
			break;
		case SET_POS:
			downloadItemW->setPos(val);
			break;
		case SET_SPEED:
			downloadItemW->setSpeed(val);
			break;
		case DOWNLOAD_ERROR:
			downloadItemW->error();
			break;
		case FINISH:
			downloadItemW->finish();
			break;
	}
}
void DownloaderThread::stop()
{
	ioCtrl.abort();
}
void DownloaderThread::finished()
{
	if (downloadItemW)
		downloadItemW->ssBEnable();
}

void DownloaderThread::run()
{
	ioCtrl.resetAbort();

	QString scheme = Functions::getUrlScheme(url);
	if (scheme.isEmpty())
		url.prepend("http://");
	else if (scheme == "file")
	{
		if (!item)
			deleteLater();
		emit QMPlay2Core.sendMessage(tr("Invalid address"), DownloaderName);
		return;
	}

	emit listSig(ADD_ENTRY);

	QString newUrl = url;
	QString extension;

	if (!prefix.isEmpty())
		for (QMPlay2Extensions *QMPlay2Ext : QMPlay2Extensions::QMPlay2ExtensionsList())
			if (QMPlay2Ext->addressPrefixList(false).contains(prefix))
			{
				newUrl.clear();
				QMPlay2Ext->convertAddress(prefix, url, param, &newUrl, &name, nullptr, &extension, &ioCtrl);
				break;
			}

	const auto getFilePath = [&]()->QString {
		QString filePath;
		quint16 num = 0;
		do
			filePath = downloadLW->getDownloadsDirPath() + (num ? (QString::number(num) + "_") : QString()) + Functions::cleanFileName(name);
		while (QFile::exists(filePath) && ++num < 0xFFFF);
		if (num == 0xFFFF)
			filePath.clear();
		return filePath;
	};

	QElapsedTimer speedT;
	const auto setBitRate = [&](const std::function<qint64()> &getPosDiff) {
		const int elapsed = speedT.elapsed();
		if (elapsed >= 1000)
		{
			emit listSig(SET_SPEED, getPosDiff() * 1000 / elapsed);
			speedT.restart();
		}
	};

	bool err = true;

	if (newUrl.startsWith("FFmpeg://"))
	{
		IOController<Demuxer> &demuxer = ioCtrl.toRef<Demuxer>();
		QMPlay2Core.setWorking(true);
		if (Demuxer::create(newUrl, demuxer) && !demuxer.isAborted())
		{
			if (name.isEmpty())
				name = demuxer->title();
			if (!name.endsWith(".mkv", Qt::CaseInsensitive))
				name += ".mkv";
			emit listSig(NAME);

			QString filePath = getFilePath();
			if (!filePath.isEmpty())
			{
				const QList<StreamInfo *> streamsInfo = demuxer->streamsInfo();
				MkvMuxer muxer(filePath, streamsInfo);
				if (muxer.isOk())
				{
					const double length = demuxer->length();
					const qint64 size = demuxer->size();
					double lastBytesPos = 0.0;
					double bytePos = 0.0;
					double pos = 0.0;

					emit listSig(SET, (size < 0) ? (length < 0 ? -1 : -2) : size, filePath);
					err = false;
					speedT.start();
					while (!demuxer.isAborted())
					{
						Packet packet;
						int idx = -1;
						if (!demuxer->read(packet, idx))
							break;
						if (idx < 0 || idx >= streamsInfo.count())
							continue;

						if (!muxer.write(packet, idx))
						{
							err = true;
							break;
						}

						bytePos += packet.size();
						setBitRate([&] {
							const qint64 tmp = bytePos - lastBytesPos;
							lastBytesPos = bytePos;
							return tmp;
						});

						if (length > 0.0)
						{
							pos = qMax<double>(pos, packet.ts);
							emit listSig(SET_POS, pos * 100 / length);
						}
					}
				}
			}
			demuxer.clear();
		}
		emit listSig(err ? DOWNLOAD_ERROR : FINISH);
		QMPlay2Core.setWorking(false);
		return;
	}

	if (name.isEmpty())
	{
		name = Functions::fileName(newUrl);
		int idx = name.indexOf("?");
		if (idx > -1)
			name.remove(idx, name.size() - idx);
	}
	else if (param.isEmpty() && extension.isEmpty())
	{
		// Extract file extension from URL if exists
		QString tmp = newUrl;
		int idx = tmp.indexOf("?");
		if (idx > -1)
			tmp.remove(idx, tmp.size() - idx);
		const int idx1 = tmp.indexOf("://");
		const int idx2 = tmp.lastIndexOf(".");
		if (idx1 > -1 && idx2 > -1 && tmp.lastIndexOf('/', idx2) != idx1 + 2)
			name += tmp.mid(idx2);
	}
	if (!name.isEmpty() && !extension.isEmpty())
		name += extension;
	emit listSig(NAME);

	if (ioCtrl.isAborted())
		return;

	QMPlay2Core.setWorking(true);

	IOController<Reader> &reader = ioCtrl.toRef<Reader>();
	if (!newUrl.isEmpty())
		Reader::create(newUrl, reader);
	if (reader && reader->readyRead() && !reader->atEnd())
	{
		QFile file(getFilePath());
		if (!file.fileName().isEmpty() && file.open(QFile::WriteOnly))
		{
			qint64 lastBytesPos = 0;
			int lastPos = -1;

			emit listSig(SET, qMax<qint64>(-1, reader->size()), file.fileName());
			err = false;
			speedT.start();
			while (!reader.isAborted() && !(err = !reader->readyRead()) && !reader->atEnd())
			{
				const QByteArray arr = reader->read(16384);
				if (arr.size())
				{
					if (file.write(arr) != arr.size())
					{
						err = true;
						break;
					}
				}
				else
				{
					if (!reader.isAborted() && ((reader->size() < 0 && !file.size()) || (reader->size() > -1 && !reader->atEnd())))
						err = true;
					break;
				}

				const qint64 bytesPos = reader->pos();
				setBitRate([&] {
					const qint64 tmp = bytesPos - lastBytesPos;
					lastBytesPos = bytesPos;
					return tmp;
				});
				if (reader->size() > 0)
				{
					const int pos = bytesPos * 100 / reader->size();
					if (pos != lastPos)
					{
						emit listSig(SET_POS, pos);
						lastPos = pos;
					}
				}
			}
		}
		reader.clear();
	}
	emit listSig(err ? DOWNLOAD_ERROR : FINISH);

	QMPlay2Core.setWorking(false);
}

QIcon DownloaderThread::getIcon()
{
	if (!prefix.isEmpty())
	{
		for (const QMPlay2Extensions *QMPlay2Ext : QMPlay2Extensions::QMPlay2ExtensionsList())
		{
			const QList<QMPlay2Extensions::AddressPrefix> addressPrefixList = QMPlay2Ext->addressPrefixList();
			const int idx = addressPrefixList.indexOf(prefix);
			if (idx > -1)
				return addressPrefixList[idx].icon;
		}
	}
	return QIcon();
}

/**/

Downloader::Downloader(Module &module) :
	downloadLW(nullptr)
{
	SetModule(module);
}
Downloader::~Downloader()
{
	if (downloadLW)
	{
		int count = 0;
		QByteArray arr;
		QDataStream stream(&arr, QIODevice::WriteOnly);
		for (QTreeWidgetItem *item : downloadLW->findItems(QString(), Qt::MatchContains))
		{
			DownloadItemW *downloadItemW = (DownloadItemW *)downloadLW->itemWidget(item, 0);
			downloadItemW->write(stream);
			++count;
		}
		Settings sets("Downloader");
		sets.set("Count", count);
		sets.set("Items", arr);
	}
}

void Downloader::init()
{
	dw = new DockWidget;
	dw->setObjectName(DownloaderName);
	dw->setWindowTitle(tr("Downloader"));
	dw->setWidget(this);

	downloadLW = new DownloadListW;
	downloadLW->setHeaderHidden(true);
	downloadLW->setRootIsDecorated(false);
	connect(downloadLW, SIGNAL(itemDoubleClicked(QTreeWidgetItem *, int)), this, SLOT(itemDoubleClicked(QTreeWidgetItem *)));

	setDownloadsDirB = new QToolButton;
	setDownloadsDirB->setIcon(QMPlay2Core.getIconFromTheme("folder-open"));
	setDownloadsDirB->setText(tr("Download directory"));
	setDownloadsDirB->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	setDownloadsDirB->setToolTip(tr("Choose directory for downloaded files"));
	connect(setDownloadsDirB, SIGNAL(clicked()), this, SLOT(setDownloadsDir()));

	clearFinishedB = new QToolButton;
	clearFinishedB->setIcon(QMPlay2Core.getIconFromTheme("archive-remove"));
	clearFinishedB->setToolTip(tr("Clear completed downloads"));
	connect(clearFinishedB, SIGNAL(clicked()), this, SLOT(clearFinished()));

	addUrlB = new QToolButton;
	addUrlB->setIcon(QMPlay2Core.getIconFromTheme("folder-new"));
	addUrlB->setToolTip(tr("Enter the address for download"));
	connect(addUrlB, SIGNAL(clicked()), this, SLOT(addUrl()));

	layout = new QGridLayout(this);
	layout->setMargin(0);
	layout->addWidget(downloadLW, 0, 0, 1, 4);
	layout->addWidget(setDownloadsDirB, 1, 0, 1, 1);
	layout->addItem(new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Minimum), 1, 1, 1, 1);
	layout->addWidget(clearFinishedB, 1, 2, 1, 1);
	layout->addWidget(addUrlB, 1, 3, 1, 1);

	Settings sets("Downloader");

	QString defDownloadPath;
#if QT_VERSION < 0x050000
	#ifdef Q_OS_WIN
		defDownloadPath = QDesktopServices::storageLocation(QDesktopServices::DocumentsLocation);
		if (defDownloadPath.isEmpty())
	#endif
			defDownloadPath = QDir::homePath();
#else
	defDownloadPath = QStandardPaths::standardLocations(QStandardPaths::DownloadLocation).value(0, QDir::homePath());
#endif
#ifdef Q_OS_WIN
	defDownloadPath.replace('\\', '/');
#endif
	downloadLW->downloadsDirPath = Functions::cleanPath(sets.getString("DownloadsDirPath", defDownloadPath));

	const int count = sets.getInt("Count");
	if (count > 0)
	{
		const QByteArray arr = sets.getByteArray("Items");
		QDataStream stream(arr);
		for (int i = 0; i < count; ++i)
			new DownloaderThread(&stream, QString(), downloadLW);
		downloadLW->setCurrentItem(downloadLW->invisibleRootItem()->child(0));
	}
}

DockWidget *Downloader::getDockWidget()
{
	return dw;
}

QVector<QAction *> Downloader::getActions(const QString &name, double, const QString &url, const QString &prefix, const QString &param)
{
	if (url.startsWith("file://"))
		return {};
	for (Module *module : QMPlay2Core.getPluginsInstance())
		for (const Module::Info &mod : module->getModulesInfo())
			if (mod.type == Module::DEMUXER && mod.name == prefix)
				return {};
	QAction *act = new QAction(Downloader::tr("Download"), nullptr);
	act->setIcon(QIcon(":/downloader.svgz"));
	act->connect(act, SIGNAL(triggered()), this, SLOT(download()));
	act->setProperty("name", name);
	if (!prefix.isEmpty())
	{
		act->setProperty("prefix", prefix);
		act->setProperty("param", param);
	}
	act->setProperty("url", url);
	return {act};
}

void Downloader::setDownloadsDir()
{
	QFileInfo dir(QFileDialog::getExistingDirectory(this, tr("Choose directory for downloaded files"), downloadLW->downloadsDirPath));
#ifndef Q_OS_WIN
	if (dir.isDir() && dir.isWritable())
#else
	if (dir.isDir())
#endif
	{
		downloadLW->downloadsDirPath = Functions::cleanPath(dir.filePath());
		Settings("Downloader").set("DownloadsDirPath", downloadLW->downloadsDirPath);
	}
	else if (dir.filePath() != QString())
		QMessageBox::warning(this, DownloaderName, tr("Cannot change downloading files directory"));
}
void Downloader::clearFinished()
{
	const QList<QTreeWidgetItem *> items = downloadLW->findItems(QString(), Qt::MatchContains);
	for (int i = items.count() - 1; i >= 0; --i)
		if (((DownloadItemW *)downloadLW->itemWidget(items[i], 0))->isFinished())
			delete items[i];
}
void Downloader::addUrl()
{
	QString clipboardUrl;
	const QMimeData *mime = QApplication::clipboard()->mimeData();
	if (mime && mime->hasText())
	{
		clipboardUrl = mime->text();
		if (clipboardUrl.contains('\n') || Functions::getUrlScheme(clipboardUrl) != "http")
			clipboardUrl.clear();
	}
	QString url = QInputDialog::getText(this, DownloaderName, tr("Enter address"), QLineEdit::Normal, clipboardUrl);
	if (!url.isEmpty())
		new DownloaderThread(nullptr, url, downloadLW);
}
void Downloader::download()
{
	new DownloaderThread
	(
		nullptr,
		sender()->property("url").toString(),
		downloadLW,
		sender()->property("name").toString(),
		sender()->property("prefix").toString(),
		sender()->property("param").toString()
	);
	downloadLW->setCurrentItem(downloadLW->invisibleRootItem()->child(0));
}
void Downloader::itemDoubleClicked(QTreeWidgetItem *item)
{
	DownloadItemW *downloadItemW = (DownloadItemW *)downloadLW->itemWidget(item, 0);
	if (!downloadItemW->getFilePath().isEmpty())
		emit QMPlay2Core.processParam("open", downloadItemW->getFilePath());
}
