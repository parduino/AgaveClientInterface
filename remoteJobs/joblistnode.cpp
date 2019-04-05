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

#include "joblistnode.h"
#include "joboperator.h"

#include "remoteJobs/jobstandarditem.h"

#include "remotedatainterface.h"

JobListNode::JobListNode(RemoteJobData newData, JobOperator * parent) : QObject(parent)
{
    myOperator = parent;
    if (myOperator == nullptr)
    {
        qFatal("Job List Node objects must have a JobOperator parent.");
    }

    setData(newData);
}

JobListNode::~JobListNode()
{
    if (!myModelRow.isEmpty())
    {
        myOperator->getItemModel()->removeRow(myModelRow.first().row());
        myModelRow.clear();
    }
}

bool JobListNode::isSameJob(RemoteJobData compareJob)
{
    return (myData.getID() == compareJob.getID());
}

void JobListNode::setData(RemoteJobData newData)
{
    bool signalChange = false;
    if ((newData.getState() != myData.getState()) && (myData.getState() != "APP_INIT"))
    {
        signalChange = true;
    }
    myData = newData;

    if (myModelRow.isEmpty())
    {
        int i = 0;
        QStandardItem * headerItem = myOperator->getItemModel()->horizontalHeaderItem(i);
        QList<QStandardItem *> newRow;
        while (headerItem != nullptr)
        {
            QString headerText = headerItem->text();
            QStandardItem * newItem = new JobStandardItem(myData, headerText);
            newRow.append(newItem);

            i++;
            headerItem = myOperator->getItemModel()->horizontalHeaderItem(i);
        }

        myOperator->getItemModel()->insertRow(0, newRow);
        for (QStandardItem * anItem : newRow)
        {
            myModelRow.append(QPersistentModelIndex(anItem->index()));
        }
    }

    updateStandardItemEntries();

    if (signalChange)
    {
        myOperator->underlyingJobChanged();
    }
}

const RemoteJobData JobListNode::getData()
{
    return myData;
}

bool JobListNode::haveDetails()
{
    return myData.detailsLoaded();
}

void JobListNode::setDetails(QMap<QString, QString> inputs, QMap<QString, QString> params)
{
    myData.setDetails(inputs, params);
    myOperator->underlyingJobChanged();
}

bool JobListNode::haveDetailTask()
{
    return (myDetailTask != nullptr);
}

void JobListNode::setDetailTask(RemoteDataReply * newTask)
{
    if (myDetailTask != nullptr)
    {
        QObject::disconnect(myDetailTask, nullptr, this, nullptr);
    }
    myDetailTask = newTask;
    QObject::connect(myDetailTask, SIGNAL(haveJobDetails(RequestState,RemoteJobData)),
                     this, SLOT(deliverJobDetails(RequestState,RemoteJobData)));
}

void JobListNode::setJobState(QString newState)
{
    if (myData.getState() == newState) return;
    myData.setState(newState);
    updateStandardItemEntries();
    myOperator->underlyingJobChanged();
}

void JobListNode::deliverJobDetails(RequestState taskState, RemoteJobData fullJobData)
{
    myDetailTask = nullptr;
    if (taskState != RequestState::GOOD)
    {
        qCDebug(jobManager, "Unable to get task details");
        return;
    }

    if (!isSameJob(fullJobData))
    {
        qCDebug(jobManager, "ERROR: Job data and detail request mismatch.");
        return;
    }

    if (fullJobData.detailsLoaded() == false)
    {
        qCDebug(jobManager, "ERROR: Job details query reply does not have details data.");
    }

    setDetails(fullJobData.getInputs(), fullJobData.getParams());
}

void JobListNode::updateStandardItemEntries()
{
    for (QPersistentModelIndex anIndex : myModelRow)
    {
        JobStandardItem * theModelEntry = dynamic_cast<JobStandardItem *>(myOperator->getItemModel()->itemFromIndex(anIndex));
        if (theModelEntry != nullptr) theModelEntry->updateText(myData);
    }
}
