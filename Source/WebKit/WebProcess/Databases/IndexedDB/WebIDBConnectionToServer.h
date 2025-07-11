/*
 * Copyright (C) 2016-2025 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include "MessageSender.h"
#include "SandboxExtension.h"
#include <WebCore/IDBConnectionToServer.h>
#include <WebCore/IDBIndexIdentifier.h>
#include <WebCore/IDBObjectStoreIdentifier.h>
#include <WebCore/ProcessIdentifier.h>
#include <optional>

namespace WebKit {

class WebIDBResult;

class WebIDBConnectionToServer final : private WebCore::IDBClient::IDBConnectionToServerDelegate, private IPC::MessageSender, public RefCounted<WebIDBConnectionToServer> {
public:
    static Ref<WebIDBConnectionToServer> create(PAL::SessionID);
    virtual ~WebIDBConnectionToServer();

    WebCore::IDBClient::IDBConnectionToServer& coreConnectionToServer();
    std::optional<WebCore::IDBConnectionIdentifier> identifier() const final;

    void didReceiveMessage(IPC::Connection&, IPC::Decoder&);
    void connectionToServerLost();

private:
    WebIDBConnectionToServer(PAL::SessionID);

    IPC::Connection* messageSenderConnection() const final;
    uint64_t messageSenderDestinationID() const final { return 0; }

    // IDBConnectionToServerDelegate
    void deleteDatabase(const WebCore::IDBOpenRequestData&) final;
    void openDatabase(const WebCore::IDBOpenRequestData&) final;
    void abortTransaction(const WebCore::IDBResourceIdentifier&) final;
    void commitTransaction(const WebCore::IDBResourceIdentifier&, uint64_t handledRequestResultsCount) final;
    void didFinishHandlingVersionChangeTransaction(WebCore::IDBDatabaseConnectionIdentifier, const WebCore::IDBResourceIdentifier&) final;
    void createObjectStore(const WebCore::IDBRequestData&, const WebCore::IDBObjectStoreInfo&) final;
    void deleteObjectStore(const WebCore::IDBRequestData&, const String& objectStoreName) final;
    void renameObjectStore(const WebCore::IDBRequestData&, WebCore::IDBObjectStoreIdentifier, const String& newName) final;
    void clearObjectStore(const WebCore::IDBRequestData&, WebCore::IDBObjectStoreIdentifier) final;
    void createIndex(const WebCore::IDBRequestData&, const WebCore::IDBIndexInfo&) final;
    void deleteIndex(const WebCore::IDBRequestData&, WebCore::IDBObjectStoreIdentifier, const String& indexName) final;
    void renameIndex(const WebCore::IDBRequestData&, WebCore::IDBObjectStoreIdentifier, WebCore::IDBIndexIdentifier, const String& newName) final;
    void putOrAdd(const WebCore::IDBRequestData&, const WebCore::IDBKeyData&, const WebCore::IDBValue&, const WebCore::IndexIDToIndexKeyMap&, const WebCore::IndexedDB::ObjectStoreOverwriteMode) final;
    void getRecord(const WebCore::IDBRequestData&, const WebCore::IDBGetRecordData&) final;
    void getAllRecords(const WebCore::IDBRequestData&, const WebCore::IDBGetAllRecordsData&) final;
    void getCount(const WebCore::IDBRequestData&, const WebCore::IDBKeyRangeData&) final;
    void deleteRecord(const WebCore::IDBRequestData&, const WebCore::IDBKeyRangeData&) final;
    void openCursor(const WebCore::IDBRequestData&, const WebCore::IDBCursorInfo&) final;
    void iterateCursor(const WebCore::IDBRequestData&, const WebCore::IDBIterateCursorData&) final;
    void establishTransaction(WebCore::IDBDatabaseConnectionIdentifier, const WebCore::IDBTransactionInfo&) final;
    void databaseConnectionPendingClose(WebCore::IDBDatabaseConnectionIdentifier) final;
    void databaseConnectionClosed(WebCore::IDBDatabaseConnectionIdentifier) final;
    void abortOpenAndUpgradeNeeded(WebCore::IDBDatabaseConnectionIdentifier, const std::optional<WebCore::IDBResourceIdentifier>& transactionIdentifier) final;
    void didFireVersionChangeEvent(WebCore::IDBDatabaseConnectionIdentifier, const WebCore::IDBResourceIdentifier& requestIdentifier, const WebCore::IndexedDB::ConnectionClosedOnBehalfOfServer) final;
    void didGenerateIndexKeyForRecord(const WebCore::IDBResourceIdentifier& transactionIdentifier, const WebCore::IDBResourceIdentifier& requestIdentifier, const WebCore::IDBIndexInfo&, const WebCore::IDBKeyData&, const WebCore::IndexKey&, std::optional<int64_t> recordID) final;
    void openDBRequestCancelled(const WebCore::IDBOpenRequestData&) final;

    void getAllDatabaseNamesAndVersions(const WebCore::IDBResourceIdentifier&, const WebCore::ClientOrigin&) final;

    // Messages received from Network Process
    void didDeleteDatabase(const WebCore::IDBResultData&);
    void didOpenDatabase(const WebCore::IDBResultData&);
    void didAbortTransaction(const WebCore::IDBResourceIdentifier& transactionIdentifier, const WebCore::IDBError&);
    void didCommitTransaction(const WebCore::IDBResourceIdentifier& transactionIdentifier, const WebCore::IDBError&);
    void didCreateObjectStore(const WebCore::IDBResultData&);
    void didDeleteObjectStore(const WebCore::IDBResultData&);
    void didRenameObjectStore(const WebCore::IDBResultData&);
    void didClearObjectStore(const WebCore::IDBResultData&);
    void didCreateIndex(const WebCore::IDBResultData&);
    void didDeleteIndex(const WebCore::IDBResultData&);
    void didRenameIndex(const WebCore::IDBResultData&);
    void didPutOrAdd(const WebCore::IDBResultData&);
    void didGetRecord(const WebIDBResult&);
    void didGetAllRecords(const WebIDBResult&);
    void didGetCount(const WebCore::IDBResultData&);
    void didDeleteRecord(const WebCore::IDBResultData&);
    void didOpenCursor(const WebIDBResult&);
    void didIterateCursor(const WebIDBResult&);
    void fireVersionChangeEvent(WebCore::IDBDatabaseConnectionIdentifier uniqueDatabaseConnectionIdentifier, const WebCore::IDBResourceIdentifier& requestIdentifier, uint64_t requestedVersion);
    void generateIndexKeyForRecord(const WebCore::IDBResourceIdentifier& requestIdentifier, const WebCore::IDBIndexInfo&, const std::optional<WebCore::IDBKeyPath>&, const WebCore::IDBKeyData&, const WebCore::IDBValue&, std::optional<int64_t> recordID);
    void didStartTransaction(const WebCore::IDBResourceIdentifier& transactionIdentifier, const WebCore::IDBError&);
    void didCloseFromServer(WebCore::IDBDatabaseConnectionIdentifier, const WebCore::IDBError&);
    void notifyOpenDBRequestBlocked(const WebCore::IDBResourceIdentifier& requestIdentifier, uint64_t oldVersion, uint64_t newVersion);
    void didGetAllDatabaseNamesAndVersions(const WebCore::IDBResourceIdentifier&, Vector<WebCore::IDBDatabaseNameAndVersion>&&);

    const Ref<WebCore::IDBClient::IDBConnectionToServer> m_connectionToServer;
};

} // namespace WebKit
