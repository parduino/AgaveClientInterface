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

#ifndef AGAVETASKREPLY_H
#define AGAVETASKREPLY_H

#include "remotedatainterface.h"

#include <QNetworkReply>

#include <QTimer>
#include <QMetaMethod>
#include <QJsonArray>

class AgaveHandler;
class AgaveTaskGuide;

class AgaveTaskReply : public RemoteDataReply
{
    Q_OBJECT

    friend class AgaveHandler;

public:
    explicit AgaveTaskReply(AgaveTaskGuide * theGuide, QNetworkReply *newReply, AgaveHandler * theManager, QObject *parent = nullptr);
    explicit AgaveTaskReply(AgaveTaskGuide * theGuide, RequestState passThruErrorState, AgaveHandler * theManager, QObject *parent = nullptr);
    ~AgaveTaskReply();

    QMap<QString, QByteArray> *getTaskParamList();
    virtual void setAsUnconnectedReply();

    //-------------------------------------------------
    //Agave specific:
    AgaveTaskGuide * getTaskGuide();

    static RequestState standardSuccessFailCheck(AgaveTaskGuide * taskGuide, QJsonDocument * parsedDoc);
    static FileMetaData parseJSONfileMetaData(QJsonObject fileNameValuePairs);
    static QList<RemoteJobData> parseJSONjobMetaData(QJsonArray rawJobList);
    static RemoteJobData parseJSONjobDetails(QJsonObject rawJobData, bool haveDetails = true);

    static QJsonValue retriveMainAgaveJSON(QJsonDocument * parsedDoc, const char * oneKey);
    static QJsonValue retriveMainAgaveJSON(QJsonDocument * parsedDoc, QString oneKey);
    static QJsonValue retriveMainAgaveJSON(QJsonDocument * parsedDoc, QList<QString> keyList);
    static QJsonValue recursiveJSONdig(QJsonValue currObj, QList<QString> * keyList, int i);

    static QDateTime parseAgaveTime(QString agaveTime);
    static QMap<QString, QString> convertVarMapToString(QMap<QString, QVariant> inMap);

signals:
    //TODO: Concerned that this might hide that passing of an implictly shared object
    //Double-check that the data is all passed.
    void haveAgaveAppList(RequestState theGuide, QVariantList appsList);

protected slots:
    void rawNoDataNoHttpTaskComplete(RequestState replyState = RequestState::GOOD);

private slots:
    void rawPassThruTaskComplete();
    void rawHttpTaskComplete();

private:
    bool performInitPointerCheck(AgaveTaskGuide * theGuide, AgaveHandler * theManager);

    void signalConnectDelay();
    bool anySignalConnect();

    void setDelayedDatalessReply(RequestState replyState);
    void processDatalessReply(RequestState replyState);

    AgaveHandler * myManager = nullptr;
    AgaveTaskGuide * myGuide = nullptr;
    QNetworkReply * myReplyObject = nullptr;

    //delayed dataless reply store:
    bool hasPendingReply = false;
    RequestState pendingReply = RequestState::INTERNAL_ERROR;

    bool expectsSignalConnect = true;

    QMap<QString, QByteArray> taskParamList;
};

#endif // AGAVETASKREPLY_H
