
/****************************************************************************
** Copyright (c) 2006 - 2011, the LibQ project.
** See the Q AUTHORS file for a list of authors and copyright holders.
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are met:
**     * Redistributions of source code must retain the above copyright
**       notice, this list of conditions and the following disclaimer.
**     * Redistributions in binary form must reproduce the above copyright
**       notice, this list of conditions and the following disclaimer in the
**       documentation and/or other materials provided with the distribution.
**     * Neither the name of the LibQ project nor the
**       names of its contributors may be used to endorse or promote products
**       derived from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
** ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
** WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
** DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
** DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
** (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
** LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
** ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
** SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**
** <http://libQ.org>  <foundation@libQ.org>
*****************************************************************************/

/*!
    \class QSshChannel
    \brief The QSshChannel class provides common functionality for QSshClient channels

    QSshChannel is the base interface class for all I/O channels created by QSshClient.

    As a QIODevice subclass, QSshChannel exposes all of the normal methods common to Qt
    I/O classes, including the readyRead() signal and the read() and write() methods.

    QSshChannel is an interface class providing the foundations for the various channel
    types offered by QSsh. It is not intended to be instantiated directly nor is it
    intended to be subclassed by user code. Use the convenience methods on QSshClient
    such as QSshClient::openProcessChannel() and QSshClient::openTcpSocket().
*/

/*!
 * \fn QSshChannel::connected()
 *
 * This signal is emitted when the channel has been successfully opened.
 */

#include "qsshchannel.h"
#include "qsshchannel_p.h"

/*! \internal */
QSshChannel::QSshChannel(QSshClient * parent)
    :QIODevice(parent)
    ,d(new QSshChannelPrivate(this,parent)){
}

/*!
 * Destroys the QSshChannel object.
 */
QSshChannel::~QSshChannel()
{
    delete d;
}

void QSshChannel::close()
{
    if (isOpen() && d->d_channel) {
        libssh2_channel_send_eof(d->d_channel);
        libssh2_channel_close(d->d_channel);
        d->d_state = 0;
    }

    QIODevice::close();
}

QSshChannelPrivate::QSshChannelPrivate(QSshChannel *_p,QSshClient * c)
    :QObject(0)
    ,p(_p)
    ,d_client(c)
    ,d_channel(0)
    ,d_session(d_client->d->d_session)
    ,d_read_stream_id(0)
    ,d_write_stream_id(0)
    ,d_state(0)
    ,d_port(0)
{
}

/*!
    \reimp
*/
qint64 QSshChannel::readData(char* buff, qint64 len) {
    ssize_t ret=libssh2_channel_read_ex(d->d_channel, d->d_read_stream_id,buff, len);
    if(ret<0){
        if(ret==LIBSSH2_ERROR_EAGAIN){
            return 0;
        }else{
#ifdef Q_DEBUG_SSH
            qDebug()<<"read err"<<ret;
#endif
            close();

            return -1;
        }
    }

    if (libssh2_channel_eof(d->d_channel)) {
        qDebug() << "SSH Channel EOF on read";
        close();
    }

    return ret;
}

/*!
    \reimp
*/
qint64 QSshChannel::writeData(const char* buff, qint64 len){
    ssize_t ret=libssh2_channel_write_ex(d->d_channel, d->d_write_stream_id,buff, len);
    if(ret<0){
        if(ret==LIBSSH2_ERROR_EAGAIN){
            return 0;
        }else{
#ifdef Q_DEBUG_SSH
            qDebug()<<"write err"<<ret;
#endif            
            close();

            return -1;
        }
    }

    if (libssh2_channel_eof(d->d_channel)) {
        qDebug() << "SSH Channel EOF on write";
        close();
    }

    return ret;
}
/*!
 * \reimp
 */
bool QSshChannel::isSequential () const{
    return true;
}

bool QSshChannelPrivate::activate(){
    //session
    if(d_state==1){
        d_channel=libssh2_channel_open_session(d_session);
        if(d_channel==NULL){
            if(libssh2_session_last_error(d_session,NULL,NULL,0)==LIBSSH2_ERROR_EAGAIN) {
                return true;
            }else{
                return false;
            }
        }
#ifdef Q_DEBUG_SSH
        qDebug("session opened");
#endif
        d_state=2;
        return activate();

    //transition to allow early cmd
    }else if (d_state==2){
        if(!d_next_actions.isEmpty()){
            d_state=d_next_actions.takeFirst();
            return activate();
        }else{
            return true;
        }

    //request pty
    }else if (d_state==5){
        int r=libssh2_channel_request_pty(d_channel,d_pty.data());
        if(r){
            if(r==LIBSSH2_ERROR_EAGAIN){
                return true;
            }else{
                qWarning("QSshChannel: pty allocation failed");
                return false;
            }
        }
#ifdef Q_DEBUG_SSH
        qDebug("pty opened");
#endif
        d_state=2;
        return activate();

    //start
    }else if (d_state==3){
        int r=libssh2_channel_exec(d_channel,qPrintable(d_cmd));
        if(r){
            if(r==LIBSSH2_ERROR_EAGAIN){
                return true;
            }else{
#ifdef Q_DEBUG_SSH
                qDebug("exec failed");
#endif
                return false;
            }
        }
#ifdef Q_DEBUG_SSH
        qDebug("exec opened");
#endif
        p->setOpenMode(QIODevice::ReadWrite);
        d_state=66;
        emit p->connected();
        return true;

    //start shell
    }else if (d_state==4){
        int r=libssh2_channel_shell(d_channel);
        if(r){
            if(r==LIBSSH2_ERROR_EAGAIN){
                return true;
            }else{
#ifdef Q_DEBUG_SSH
                qDebug("exec failed");
#endif
                return false;
            }
        }
#ifdef Q_DEBUG_SSH
        qDebug("shell opened");
#endif
        p->setOpenMode(QIODevice::ReadWrite);
        d_state=9999;
        emit p->connected();
        return true;

    // tcp channel
    }else if (d_state==10){
        d_channel=libssh2_channel_direct_tcpip(d_session, qPrintable(d_host),d_port);
        if(d_channel==NULL){
            if(libssh2_session_last_error(d_session,NULL,NULL,0)==LIBSSH2_ERROR_EAGAIN) {
                return true;
            }else{
                return false;
            }
        }
#ifdef Q_DEBUG_SSH
        qDebug("tcp channel opened");
#endif
        p->setOpenMode(QIODevice::ReadWrite);
        d_state=9999;
        return activate();

    //read channel
    }else if (d_state==9999){
        emit p->readyRead();
    }
    return true;
}


void QSshChannelPrivate::openSession(){
    if(d_state!=0){
        return;
    }
    d_state=1;
    activate();
}

void QSshChannelPrivate::requestPty(QByteArray pty){
    if(d_state>5){
        return;
    }
    d_pty=pty;
    if(d_state==2){
        d_state=5;
        activate();
    }else{
        if(!d_next_actions.contains(5))
            d_next_actions.append(5);
    }
}

void QSshChannelPrivate::start(QString cmd){
    if(d_state>5){
        return;
    }
    d_cmd=cmd;
    if(d_state==2){
        d_state=3;
        activate();
    }else{
        if(!d_next_actions.contains(3))
            d_next_actions.append(3);
    }
}
void QSshChannelPrivate::startShell(){
    if(d_state>5){
        return;
    }
    if(d_state==2){
        d_state=4;
        activate();
    }else{
        if(!d_next_actions.contains(4))
            d_next_actions.append(4);
    }
}



void QSshChannelPrivate::openTcpSocket(QString host,qint16 port){
    if(d_state!=0){
        return;
    }
    d_host=host;
    d_port=port;
    d_state=10;
    activate();
}
