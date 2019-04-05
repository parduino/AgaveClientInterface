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

#include "joboperator.h"

#include "remoteJobs/remotejoblister.h"
#include "remotedatainterface.h"
#include "remotejobdata.h"
#include "joblistnode.h"

Q_LOGGING_CATEGORY(jobManager, "Job Manager")

JobOperator::JobOperator(RemoteDataInterface * theDataInterface, QObject *parent) : QObject(qobject_cast<QObject *>(parent))
{
    myInterface = theDataInterface;
    if (myInterface == nullptr)
    {
        qFatal("Cannot create JobOperator object with null remote interface.");
    }
    theJobList.setHorizontalHeaderLabels({"Task Name", "State", "Agave App", "Time Created", "Agave ID"});
    QObject::connect(myInterface, SIGNAL(connectionStateChanged(RemoteDataInterfaceState)), this, SLOT(interfaceHasNewState(RemoteDataInterfaceState)));

    interfaceHasNewState(myInterface->getInterfaceState());
}

JobOperator::~JobOperator()
{
    while (!linkedListerWidgets.isEmpty())
    {
        RemoteJobLister * aListerWidget = linkedListerWidgets.takeLast();
        aListerWidget->setModel(nullptr);
    }

    for (auto itr = jobData.begin(); itr != jobData.end(); itr++)
    {
        delete (*itr);
    }
}

void JobOperator::linkToJobLister(RemoteJobLister * newLister)
{
    if (linkedListerWidgets.contains(newLister)) return;

    linkedListerWidgets.append(newLister);
    newLister->setModel(&theJobList);
}

void JobOperator::disconnectJobLister(RemoteJobLister * oldLister)
{
    if (!linkedListerWidgets.contains(oldLister)) return;
    linkedListerWidgets.removeAll(oldLister);
    oldLister->setModel(nullptr);
}

void JobOperator::refreshRunningJobList(RequestState replyState, QList<RemoteJobData> theData)
{
    //Note: RemoteDataReply destroys itself after signal
    currentJobRefreshReply = nullptr;
    if (replyState != RequestState::GOOD)
    {
        qCDebug(jobManager, "Error: unable to list jobs. Bad reply from agave connection.");
        //TODO: Add more error passing
        QTimer::singleShot(5000, this, SLOT(demandJobDataRefresh()));
        return;
    }

    bool notDone = false;

    QList<QString> toDel;
    for (auto itr = jobData.begin(); itr != jobData.end(); itr++)
    {
        if (listHasJobId(theData, itr.key()))
        {
            continue;
        }
        toDel.append(itr.key());
    }

    for (QString jobID : toDel)
    {
        JobListNode * toDel = jobData.take(jobID);
        toDel->deleteLater();
    }

    for (auto itr = theData.rbegin(); itr != theData.rend(); itr++)
    {
        if (jobData.contains((*itr).getID()))
        {
            JobListNode * theItem = jobData.value((*itr).getID());
            theItem->setJobState((*itr).getState());
        }
        else
        {
            JobListNode * theItem = new JobListNode(*itr, this);
            jobData.insert(theItem->getData().getID(), theItem);
        }
        if (!notDone && (!(*itr).inTerminalState()))
        {
            notDone = true;
        }
    }

    emit newJobData();

    if (notDone)
    {
        QTimer::singleShot(5000, this, SLOT(demandJobDataRefresh()));
    }
}

void JobOperator::jobOperationFollowup(RequestState replyState)
{
    currentJobOpReply = nullptr;

    if (replyState == RequestState::GOOD)
    {
        emit jobOpDone(replyState, "Job Operation Complete");
    }
    else
    {
        QString error = "Job Operation Failed: ";
        error = error.append(RemoteDataInterface::interpretRequestState(replyState));
        qCDebug(jobManager, "%s", qPrintable(error));
        emit jobOpDone(replyState, error);
    }
    demandJobDataRefresh();
}

bool JobOperator::listHasJobId(QList<RemoteJobData> theData, QString toFind)
{
    for (auto itr = theData.rbegin(); itr != theData.rend(); itr++)
    {
        if (toFind == (*itr).getID())
        {
            return true;
        }
    }
    return false;
}

QMap<QString, RemoteJobData> JobOperator::getJobsList()
{
    QMap<QString, RemoteJobData> ret;

    for (auto itr = jobData.cbegin(); itr != jobData.cend(); itr++)
    {
        ret.insert((*itr)->getData().getID(), (*itr)->getData());
    }

    return ret;
}

void JobOperator::requestJobDetails(const RemoteJobData *toFetch)
{
    JobListNode * realNode = getRealNode(toFetch);

    if (realNode == nullptr) return;
    if (realNode->haveDetails()) return;
    if (realNode->haveDetailTask()) return;

    RemoteDataReply * jobReply = myInterface->getJobDetails(realNode->getData().getID());

    if (jobReply == nullptr) return; //TODO: Consider an error message here

    realNode->setDetailTask(jobReply);
}

void JobOperator::deleteJobDataEntry(const RemoteJobData *toDelete)
{
    JobListNode * realNode = getRealNode(toDelete);

    if (realNode == nullptr) return;

    RemoteDataReply * jobReply = myInterface->deleteJob(realNode->getData().getID());
    QObject::connect(jobReply, SIGNAL(haveDeletedJob(RequestState)), this, SLOT(jobOperationFollowup(RequestState)));
    emit jobOpStarted();
}

JobListNode * JobOperator::getRealNode(const RemoteJobData *toFetch)
{
    if (!jobData.contains(toFetch->getID()))
    {
        return nullptr;
    }
    return jobData.value(toFetch->getID());
}

void JobOperator::underlyingJobChanged()
{
    emit newJobData();
}

QStandardItemModel * JobOperator::getItemModel()
{
    return &theJobList;
}

const RemoteJobData JobOperator::findJobByID(QString idToFind)
{
    if (!jobData.contains(idToFind))
    {
        return RemoteJobData::nil();
    }

    return jobData.value(idToFind)->getData();
}

void JobOperator::demandJobDataRefresh()
{
    if (currentlyRefreshingJobs())
    {
        return;
    }
    currentJobRefreshReply = myInterface->getListOfJobs();
    QObject::connect(currentJobRefreshReply, SIGNAL(haveJobList(RequestState,QList<RemoteJobData>)),
                     this, SLOT(refreshRunningJobList(RequestState,QList<RemoteJobData>)));
}

bool JobOperator::currentlyRefreshingJobs()
{
    return (currentJobRefreshReply != nullptr);
}

bool JobOperator::currentlyPerformingJobOperation()
{
    return (currentJobOpReply != nullptr);
}

void JobOperator::interfaceHasNewState(RemoteDataInterfaceState newState)
{
    if (newState != RemoteDataInterfaceState::CONNECTED) return;

    demandJobDataRefresh();
}
