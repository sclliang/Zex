#include "theme.h"
#include <QFile>
#include <QHash>
#include <QIconEngine>
#include <QPainter>
#include <QPixmapCache>
#include <QSvgRenderer>
#include <memory>

namespace {
class ChromeIconEngine final : public QIconEngine {
public:
    ChromeIconEngine(QString name,QColor color):name_(std::move(name)),color_(color) {
        if(name_!="zex") {
            QFile file(":/chrome/"+name_+".svg");
            if(!file.open(QIODevice::ReadOnly)){file.setFileName(":/chrome/file.svg");file.open(QIODevice::ReadOnly);}
            auto svg=file.readAll();svg.replace("currentColor",color_.name().toUtf8());
            renderer_=std::make_shared<QSvgRenderer>(svg);
        }
    }
    QIconEngine* clone() const override {return new ChromeIconEngine(*this);}
    bool isNull() override {return name_!="zex" && (!renderer_ || !renderer_->isValid());}
    QString key() const override {return "ZexVectorIcon";}
    void paint(QPainter* painter,const QRect& rect,QIcon::Mode mode,QIcon::State) override {
        painter->save();if(mode==QIcon::Disabled)painter->setOpacity(painter->opacity()*0.38);
        if(name_=="zex") {
            painter->setRenderHint(QPainter::Antialiasing);painter->translate(rect.topLeft());painter->scale(rect.width()/20.0,rect.height()/20.0);
            painter->setPen(QPen(color_,1.8,Qt::SolidLine,Qt::SquareCap,Qt::MiterJoin));
            painter->drawPolyline(QPolygonF({{4,4},{15,4},{4,16},{16,16}}));painter->drawLine(QPointF(8,10),QPointF(15,10));
        } else if(renderer_)renderer_->render(painter,rect);
        painter->restore();
    }
    QPixmap pixmap(const QSize& size,QIcon::Mode mode,QIcon::State state) override {return scaledPixmap(size,mode,state,1);}
    QPixmap scaledPixmap(const QSize& size,QIcon::Mode mode,QIcon::State state,qreal scale) override {
        const auto key=QStringLiteral("zex/chrome/%1/%2/%3/%4/%5/%6").arg(name_,color_.name(QColor::HexArgb)).arg(size.width()).arg(size.height()).arg(int(mode)).arg(scale);
        QPixmap pix;if(QPixmapCache::find(key,&pix))return pix;
        pix=QPixmap(size*scale);pix.setDevicePixelRatio(scale);pix.fill(Qt::transparent);
        QPainter painter(&pix);paint(&painter,QRect(QPoint(),size),mode,state);painter.end();QPixmapCache::insert(key,pix);return pix;
    }
private:
    QString name_;QColor color_;std::shared_ptr<QSvgRenderer> renderer_;
};
}

QIcon Theme::icon(const QString& name,QColor color,int size) {
    Q_UNUSED(size);
    static QHash<QString,QIcon> cache;
    const auto key=name+color.name(QColor::HexArgb);
    if(auto it=cache.constFind(key);it!=cache.cend())return *it;
    auto icon=QIcon(new ChromeIconEngine(name,color));cache.insert(key,icon);return icon;
}
