/*********************************************************************************
**
** Copyright (c) 2018 The University of Notre Dame
** Copyright (c) 2018 The Regents of the University of California
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

#ifndef REMOTEFILEMODEL_H
#define REMOTEFILEMODEL_H

#include <QObject>
#include <QStandardItemModel>
#include <QStandardItem>
#include <QHeaderView>

#include "filenoderef.h"

class FileNodeRef;
class FileOperator;
class RemoteFileItem;
class RemoteFileTree;

class RemoteFileModel : public QObject
{
    Q_OBJECT

    friend class FileOperator;

public:
    RemoteFileModel(QObject *parent = nullptr);

protected:
    RemoteFileItem * getItemByFile(FileNodeRef toFind);
    QStandardItemModel * getRawModel();

private slots:
    void newFileData(FileNodeRef newFileData);

private:
    void setRootItem(FileNodeRef rootFile);
    void purgeItem(FileNodeRef toRemove);
    void updateItem(FileNodeRef toUpdate, bool folderContentsLoaded = false);
    QList<RemoteFileItem *> createItemList(FileNodeRef theFileNode);

    RemoteFileItem * findTargetItem(RemoteFileItem * parentItem, FileNodeRef toFind);
    RemoteFileItem * findParentItem(FileNodeRef toFind);
    QString getRawColumnData(FileNodeRef fileData, int i);
    static QList<QStandardItem *> demoteList(QList<RemoteFileItem *> inputList);
    static QStringList separateFilePathParts(QString thePath);
    void updateItemList(QList<RemoteFileItem *> theList, FileNodeRef newFileInfo);

    QStandardItemModel theModel;
    RemoteFileItem * userRoot = nullptr;

    //const int tableNumCols = 7;
    //const QStringList shownHeaderLabelList = {"File Name","Type","Size","Last Changed",
    //                               "Format","mimeType","Permissions"};
    const int tableNumCols = 3;
    const QStringList shownHeaderLabelList = {"File Name","Type","Size"};
};

#endif // REMOTEFILEMODEL_H
