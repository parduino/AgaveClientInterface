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

#ifndef FILERECURSIVEOPERATOR_H
#define FILERECURSIVEOPERATOR_H

#include <QObject>
#include <QDir>

#include "filemetadata.h"
#include "filenoderef.h"

class FileOperator;
class FileNodeRef;

enum class RequestState;
enum class RecursiveErrorCodes {NONE, MKDIR_FAIL, UPLOAD_FAIL, TYPE_MISSMATCH, LOST_FILE};
enum class RecursiveOpState {IDLE, REC_UPLOAD, REC_DOWNLOAD};

class FileRecursiveOperator : public QObject
{
    Q_OBJECT

    friend class FileOperator;
public:
    explicit FileRecursiveOperator(FileOperator *parent);

    RecursiveOpState getState();
    void enactRecursiveDownload(const FileNodeRef &targetFolder, QString containingDestFolder);
    void enactRecursiveUpload(const FileNodeRef &containingDestFolder, QString localFolderToCopy);
    void abortRecursiveProcess();

signals:
    //Note: it is very important that connections for these signals be queued
    void fileOpStarted();
    void fileOpDone(RequestState opState, QString err_msg);
    void newFileInterlockSignal();

private slots:
    void newFileSystemDataInterlock(FileNodeRef);
    void newFileSystemData();

protected:
    void getRecursiveUploadReply(RequestState replyState, FileMetaData newFileData);
    void getRecursiveMkdirReply(RequestState replyState, FileMetaData newFolderData);

private:
    void recursiveDownloadProcessRetry();
    bool recursiveDownloadRetrivalHelper(const FileNodeRef &nodeToCheck); //Return true if have all data
    bool recursiveDownloadFolderEmitHelper(QDir currentLocalDir, const FileNodeRef &nodeToGet, RecursiveErrorCodes &errNum); //Return true if successful file output data
    bool emitBufferToFile(QDir containingDir, const FileNodeRef &nodeToGet, RecursiveErrorCodes &errNum); //Return true if successful file output data

    void recursiveUploadProcessRetry();
    bool recursiveUploadHelper(const FileNodeRef &nodeToSend, QDir localPath, RecursiveErrorCodes &errNum); //Return true if all data sent and ls verified

    void emitStdFileOpErr(QString errString, RequestState errState);

    FileOperator * myOperator;
    bool interlockHasFileChange = false;

    RecursiveOpState myState;

    QDir recursiveLocalHead;
    FileNodeRef recursiveRemoteHead;
};

#endif // FILERECURSIVEOPERATOR_H
