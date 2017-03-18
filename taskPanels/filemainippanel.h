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

#ifndef FILEMAINIPPANEL_H
#define FILEMAINIPPANEL_H

#include "taskpanelentry.h"

#include <QModelIndex>
#include <QLineEdit>
#include <QPushButton>
#include <QJsonValue>

class FileMetaData;
class FileTreeModelReader;
class RemoteDataInterface;
enum class RequestState;

class FileMainipPanel : public TaskPanelEntry
{
    Q_OBJECT
public:
    FileMainipPanel(RemoteDataInterface * newDataHandle, FileTreeModelReader * newReader, QObject *parent = 0);

    virtual void setupOwnFrame();
    virtual void frameNowVisible();
    virtual void frameNowInvisible();

private slots:
    void selectedFileChanged(FileMetaData * newSelection);

private:    
    QModelIndex currentFileSelected;
    FileTreeModelReader * myTreeReader;

    RemoteDataInterface * dataConnection;

    QLabel * contentLabel = NULL;
};

#endif // FILEMAINIPPANEL_H