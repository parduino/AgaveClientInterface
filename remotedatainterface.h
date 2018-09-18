/*********************************************************************************
**
** Copyright (c) 2017 The University of Notre Dame
** Copyright (c) 2017 The Regents of the University of California
**
** Redistribution and use in source and binary forms, with or without modification,
** are permitted provided that the following conditions are met:
**
** 1. Redistributions of source code must retain the above copyright notice, this
** list of conditions and the following disclaimer.
**
** 2. Redistributions in binary form must reproduce the above copyright notice, this
** list of conditions and the following disclaimer in the documentation and/or other
** materials provided with the distribution.
**
** 3. Neither the name of the copyright holder nor the names of its contributors may
** be used to endorse or promote products derived from this software without specific
** prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
** EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
** SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
** TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
** BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
** CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
** IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
** SUCH DAMAGE.
**
***********************************************************************************/

// Contributors:
// Written by Peter Sempolinski, for the Natural Hazard Modeling Laboratory, director: Ahsan Kareem, at Notre Dame

#ifndef REMOTEDATAINTERFACE_H
#define REMOTEDATAINTERFACE_H

#include "filemetadata.h"
#include "remotejobdata.h"

#include <QThread>
#include <QMutex>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QNetworkAccessManager>

typedef QMap<QString, QString> ParamMap;
Q_DECLARE_METATYPE(ParamMap)

Q_DECLARE_LOGGING_CATEGORY(remoteInterface)
Q_DECLARE_LOGGING_CATEGORY(rawHTTP)

enum class RemoteDataInterfaceState {INIT, READY_TO_AUTH, AUTH_TRY, CANCEL_AUTH, CONNECTED, DISCONNECTING, DISCONNECTED};

enum class RequestState {GOOD, PENDING,
                         UNKNOWN_TASK, INTERNAL_ERROR, INVALID_STATE,
                         SIGNAL_OBJ_MISMATCH, SERVICE_UNAVAILABLE,
                         LOST_INTERNET, DROPPED_CONNECTION,
                         NO_CHANGE_DIR, FILE_NOT_FOUND,
                         JOB_SYSTEM_DOWN, BAD_HTTP_REQUEST,
                         GENERIC_NETWORK_ERROR, REMOTE_SERVER_ERROR,
                         LOCAL_FILE_ERROR, JSON_PARSE_ERROR,
                         EXPLICIT_ERROR, MISSING_REPLY_STATUS,
                         MISSING_REPLY_DATA, STOPPED_BY_USER,
                         INVALID_PARAM, NOT_READY,
                         NOT_IMPLEMENTED, UNCLASSIFIED};
//If RemoteDataReply returned is nullptr, then the request was invalid due to internal error

class RemoteDataReply : public QObject
{
    Q_OBJECT

public:
    RemoteDataReply(QObject * parent);
    virtual void setAsUnconnectedReply() = 0;

signals:
    //All referenced values should be copied by the reciever or they will be discarded
    void startedLogout(RequestState replyState);

    void haveAuthReply(RequestState authReply);
    void haveLSReply(RequestState replyState, QList<FileMetaData> fileDataList);

    void haveDeleteReply(RequestState replyState, QString toDelete);
    void haveMoveReply(RequestState replyState, FileMetaData revisedFileData, QString from);
    void haveCopyReply(RequestState replyState, FileMetaData newFileData);
    void haveRenameReply(RequestState replyState, FileMetaData newFileData, QString oldName);

    void haveMkdirReply(RequestState replyState, FileMetaData newFolderData);

    void haveUploadReply(RequestState replyState, FileMetaData newFileData);
    void haveDownloadReply(RequestState replyState, QString localDest);
    void haveBufferDownloadReply(RequestState replyState, QByteArray fileBuffer);

    //Job replys should be in an intelligble format, JSON is used by Agave and AWS for various things
    void haveJobReply(RequestState replyState, QJsonDocument rawJobReply);

    void haveJobList(RequestState replyState, QList<RemoteJobData> jobList);
    void haveJobDetails(RequestState replyState, RemoteJobData jobData);
    void haveStoppedJob(RequestState replyState);
    void haveDeletedJob(RequestState replyState);
};

class RemoteDataInterface : public QObject
{
    Q_OBJECT

public:
    RemoteDataInterface(QObject * parent = nullptr);

public slots:
    virtual QString getUserName() = 0;

    //Defaults to directory root,
    //Subsequent commands with remote folder names are either absolute paths
    //or reletive to the current working directory
    virtual RemoteDataReply * closeAllConnections() = 0;

    //Remote tasks to be implemented in subclasses:
    //Returns a RemoteDataReply, which should have the correct signal attached to an appropriate slot
    //These methods should ALWAYS return a valid pointer
    virtual RemoteDataReply * performAuth(QString uname, QString passwd) = 0;

    virtual RemoteDataReply * remoteLS(QString dirPath) = 0;

    virtual RemoteDataReply * deleteFile(QString toDelete) = 0;
    virtual RemoteDataReply * moveFile(QString from, QString to) = 0;
    virtual RemoteDataReply * copyFile(QString from, QString to) = 0;
    virtual RemoteDataReply * renameFile(QString fullName, QString newName) = 0;

    virtual RemoteDataReply * mkRemoteDir(QString location, QString newName) = 0;

    virtual RemoteDataReply * uploadFile(QString location, QString localFileName) = 0;
    virtual RemoteDataReply * uploadBuffer(QString location, QByteArray fileData, QString newFileName) = 0;
    virtual RemoteDataReply * downloadFile(QString localDest, QString remoteName) = 0;
    virtual RemoteDataReply * downloadBuffer(QString remoteName) = 0;

    virtual RemoteDataReply * runRemoteJob(QString jobName, ParamMap jobParameters, QString remoteWorkingDir, QString indivJobName = "", QString archivePath = "") = 0;

    virtual RemoteDataReply * getListOfJobs() = 0;
    virtual RemoteDataReply * getJobDetails(QString IDstr) = 0;
    virtual RemoteDataReply * stopJob(QString IDstr) = 0;
    virtual RemoteDataReply * deleteJob(QString IDstr) = 0;

    virtual RemoteDataInterfaceState getInterfaceState() = 0;

    static QString interpretRequestState(RequestState theState);
    static QString removeDoubleSlashes(QString stringIn);

signals:
    void connectionStateChanged(RemoteDataInterfaceState newState);
};

#endif // REMOTEDATAINTERFACE_H
