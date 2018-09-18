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

#ifndef JOBOPERATOR_H
#define JOBOPERATOR_H

#include "remotejobdata.h"

#include <QObject>
#include <QMap>
#include <QStandardItemModel>
#include <QTimer>
#include <QLoggingCategory>

class RemoteFileWindow;
class RemoteDataInterface;
class RemoteJobLister;
class JobListNode;
class RemoteDataReply;

enum class RemoteDataInterfaceState;
enum class RequestState;

Q_DECLARE_LOGGING_CATEGORY(jobManager)

class JobOperator : public QObject
{
    Q_OBJECT

    friend class JobListNode;
    friend class RemoteJobLister;

public:
    explicit JobOperator(RemoteDataInterface * theDataInterface, QObject * parent);
    ~JobOperator();

    void requestJobDetails(const RemoteJobData *toFetch);
    void deleteJobDataEntry(const RemoteJobData *toDelete);

    QMap<QString, RemoteJobData> getJobsList();
    const RemoteJobData findJobByID(QString idToFind);

    void demandJobDataRefresh();
    bool currentlyRefreshingJobs();
    bool currentlyPerformingJobOperation();

signals:
    void newJobData();
    void jobOpStarted();
    void jobOpDone(RequestState opState, QString err_msg);

public slots:
    void interfaceHasNewState(RemoteDataInterfaceState newState);

protected:
    void linkToJobLister(RemoteJobLister * newLister);
    void disconnectJobLister(RemoteJobLister * oldLister);

    void underlyingJobChanged();
    QStandardItemModel * getItemModel();

private slots:
    void refreshRunningJobList(RequestState replyState, QList<RemoteJobData> theData);
    void jobOperationFollowup(RequestState replyState);

private:
    static bool listHasJobId(QList<RemoteJobData> theData, QString toFind);
    JobListNode * getRealNode(const RemoteJobData *toFetch);

    RemoteDataInterface * myInterface;

    QMap<QString, JobListNode *> jobData;
    RemoteDataReply * currentJobRefreshReply = nullptr;
    RemoteDataReply * currentJobOpReply = nullptr;

    QStandardItemModel theJobList;

    QList<RemoteJobLister *> linkedListerWidgets;
};

#endif // JOBOPERATOR_H
