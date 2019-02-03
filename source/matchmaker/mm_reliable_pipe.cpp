#include "mm_reliable_pipe.h"

#include "../qalgo/SingletonHolder.h"

#include "../qcommon/qthreads.h"
#include "../qcommon/qcommon.h"

const char *ReliablePipe::MakeLocalStoragePath() {
	return va( "%s/mm_db_for_port_%d", FS_CacheDirectory(), (unsigned)Cvar_Value( "sv_port" ) );
}

void *ReliablePipe::BackgroundRunner::ThreadFunc( void *param ) {
	// Wait for launching runner loops more for a listen server.
	// We do not want losing messages below in video/other console spam.
	Sys_Sleep( ( Cvar_Value( "dedicated" ) != 0.0f ) ? 3 * 1000 : 15 * 1000 );

	auto *const runner = (BackgroundRunner *)param;
	Com_Printf( "Launching matchmaker/stats ReliablePipe::%s...\n", runner->logTag );
	runner->RunMessageLoop();
	Com_Printf( "Stopping matchmaker/stats ReliablePipe::%s...\n", runner->logTag );
	return nullptr;
}

void ReliablePipe::BackgroundRunner::RunMessageLoop() {
	while( !CanTerminate() ) {
		RunStep();
	}
}

ReliablePipe::ReliablePipe()
	: reliableStorage( MakeLocalStoragePath() ) {
	// Never actually fails?
	this->reportsPipe = QBufPipe_Create( 128, 1 );

	// Never actually fails?
	this->backgroundWriter = new( ::malloc( sizeof( BackgroundWriter ) ) )BackgroundWriter( &reliableStorage, reportsPipe );
	this->backgroundSender = new( ::malloc( sizeof( BackgroundSender ) ) )BackgroundSender( &reliableStorage );

	// Never returns on fail?
	this->writerThread = QThread_Create( &BackgroundWriter::ThreadFunc, backgroundWriter );
	this->senderThread = QThread_Create( &BackgroundRunner::ThreadFunc, backgroundSender );
}

ReliablePipe::~ReliablePipe() {
	backgroundWriter->SignalForTermination();
	backgroundSender->SignalForTermination();

	QThread_Join( writerThread );
	QThread_Join( senderThread );

	backgroundWriter->~BackgroundWriter();
	backgroundSender->~BackgroundSender();

	::free( backgroundWriter );
	::free( backgroundSender );

	QBufPipe_Destroy( &reportsPipe );
}

void ReliablePipe::EnqueueMatchReport( QueryObject *matchReport ) {
	BackgroundWriter::AddReportCmd cmd( backgroundWriter, matchReport );
	QBufPipe_WriteCmd( reportsPipe, &cmd, sizeof( cmd ) );
}

ReliablePipe::BackgroundWriter::Handler ReliablePipe::BackgroundWriter::pipeHandlers[1] = {
	&ReliablePipe::BackgroundWriter::AddReportHandler
};

unsigned ReliablePipe::BackgroundWriter::AddReportHandler( const void *data ) {
	AddReportCmd cmd;
	memcpy( &cmd, data, sizeof( AddReportCmd ) );
	// Can block for a substantial amount of time
	// (for several seconds awaiting for completion of uploader thread transaction)
	cmd.self->AddReport( cmd.report );
	return (unsigned)sizeof( AddReportCmd );
}

void ReliablePipe::BackgroundWriter::RunStep() {
	Sys_Sleep( 32 );

	QBufPipe_ReadCmds( pipe, pipeHandlers );
}

void ReliablePipe::BackgroundWriter::RunMessageLoop() {
	// Run the regular inherited message loop
	ReliablePipe::BackgroundRunner::RunMessageLoop();

	// Make sure all reports get written to the database.
	// They could be still enqueued as the network uploader that could hold transactions
	// has much lower throughput compared to writing to disk locally.
	// At this moment the uploader is either terminated
	// or won't do another read-and-upload attempt, hence the database is not going to be locked.
	// This call blocks until all reports (if any) are written to the database.
	QBufPipe_ReadCmds( pipe, pipeHandlers );
}

void ReliablePipe::BackgroundWriter::AddReport( QueryObject *report ) {
	bool hasInsertionSucceeded = false;
	auto block = [&]( DBConnection connection ) {
		hasInsertionSucceeded = reliableStorage->Push( connection, report );
		// Returning true means the transaction should be committed
		return true;
	};

	for(;; ) {
		bool hasTransactionSucceeded = reliableStorage->WithinTransaction( block );
		// TODO: investigate SQLite behaviour... this code is based purely on MVCC RDBMS habits...
		if( hasTransactionSucceeded ) {
			// TODO: can insertion really fail?
			if( !hasInsertionSucceeded ) {
				Com_Printf( S_COLOR_RED "ReliablePipe::BackgroundWriter::AddReport(): Dropping a report\n" );
			}
			QueryObject::DeleteQuery( report );
			return;
		}

		// Wait for an opportunity for writing given us by the uploader thread time-to-time.
		// There should not be a substantial database lock contention.
		Sys_Sleep( 16 );
	}
}

void ReliablePipe::BackgroundSender::RunStep() {
	Sys_Sleep( 16 );

	unsigned sleepInterval = 667;
	reliableStorage->WithinTransaction( [&]( DBConnection connection ) {
		if( !( activeQuery = reliableStorage->FetchNext( connection ) ) ) {
			// No active report is present in the database yet.
			// Writer threads have plenty of time for performing their transactions in this case
			sleepInterval = 1500;
			// Returning true means the transaction should be committed
			return true;
		}

		activeQuery->SendForStatusPolling();
		while( !activeQuery->IsReady() ) {
			Sys_Sleep( 16 );
			QueryObject::Poll();
		}

		if( activeQuery->HasSucceeded() ) {
			sleepInterval = 1500;
			// Request committing or rolling back depending of result of this call
			bool result = reliableStorage->MarkAsSent( connection, activeQuery );
			DeleteActiveQuery();
			return result;
		}

		// This is more useful for non-persistent/reliable queries like client login.
		// In this scenario let's retry implicitly on next RunStep() call.
		// Fetching the same report (with the same id) is not guaranteed
		// but we should not rely on reports ordering.
		// This is to give a breathe for writer thread that is possibly tries
		// to open a transaction while we're still holding a database exclusive lock.
		if( activeQuery->ShouldRetry() ) {
			DeleteActiveQuery();
			return true;
		}

		assert( activeQuery->IsReady() && !activeQuery->HasSucceeded() && !activeQuery->ShouldRetry() );

		// Request committing or rolling back depending of result of this call
		bool result = reliableStorage->MarkAsFailed( connection, activeQuery );
		DeleteActiveQuery();
		return result;
	});

	Sys_Sleep( sleepInterval );
}
