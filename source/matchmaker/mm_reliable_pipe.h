#ifndef QFUSION_MM_REPORTS_UPLOADER_H
#define QFUSION_MM_REPORTS_UPLOADER_H

#include "mm_local_storage.h"

class ReliablePipe {
	friend class SVStatsowFacade;

	/**
	 * A common supertype for things that run in a background thread
	 * sleeping and checking their state periodically
	 * doing that until signaled for termination.
	 */
	class BackgroundRunner {
		friend class ReliablePipe;
	protected:
		std::atomic<bool> signaledForTermination { false };
		const char *const logTag;
		LocalReliableStorage *const reliableStorage;

		BackgroundRunner( const char *logTag_, LocalReliableStorage *reliableStorage_ )
			: logTag( logTag_ ), reliableStorage( reliableStorage_ ) {}

		virtual ~BackgroundRunner() = default;

		virtual bool CanTerminate() const {
			return signaledForTermination.load( std::memory_order_relaxed );
		}

		virtual void RunMessageLoop();

		virtual void RunStep() = 0;

		static void *ThreadFunc( void *param );

		void SignalForTermination() {
			signaledForTermination.store( true, std::memory_order_relaxed );
		}
	};

	/**
	 * A {@code BackgroundRunner} that listens for match reports
	 * delivered via a buffered pipe and tries to store reports in a transaction.
	 */
	class BackgroundWriter final : public BackgroundRunner {
		struct qbufPipe_s *const pipe;
	public:
		BackgroundWriter( LocalReliableStorage *reliableStorage_, struct qbufPipe_s *pipe_ )
			: BackgroundRunner( "BackgroundWriter", reliableStorage_ ), pipe( pipe_ ) {}

		struct AddReportCmd {
			int id;
			BackgroundWriter *self;
			QueryObject *report;

			AddReportCmd() = default;
			AddReportCmd( BackgroundWriter *self_, QueryObject *report_ ): id( 0 ), self( self_ ), report( report_ ) {}
		};

		void AddReport( QueryObject *report );

		void RunMessageLoop() override;

		void RunStep() override;

		typedef unsigned ( *Handler )( const void * );
		static Handler pipeHandlers[1];

		static unsigned AddReportHandler( const void * );
	};

	/**
	 * A {@code BackgroundRunner} that wraps in a transaction
	 * reading non-sent queries from a storage, sending queries
	 * over network and marking report delivery status in the storage.
	 */
	class BackgroundSender final : public BackgroundRunner {
		/**
		 * A query we try to fill using form name-value pairs stored in database.
		 * @note do not confuse with {@code BackgroundWriter::activeReport}.
		 */
		QueryObject *activeQuery { nullptr };
	public:
		explicit BackgroundSender( LocalReliableStorage *reliableStorage_ )
			: BackgroundRunner( "BackgroundSender", reliableStorage_ ) {}

		~BackgroundSender() override {
			if( activeQuery ) {
				QueryObject::DeleteQuery( activeQuery );
			}
		}

		/**
		 * An active query could fail even if the network call result is successful.
		 * @return true if the server has really accepted the query.
		 * Otherwise the query should be considered failed.
		 */
		bool CheckQueryResponse();

		void RunStep() override;

		void DeleteActiveQuery() {
			assert( activeQuery );
			QueryObject::DeleteQuery( activeQuery );
			activeQuery = nullptr;
		}
	};

	LocalReliableStorage reliableStorage;

	BackgroundWriter *backgroundWriter { nullptr };
	BackgroundSender *backgroundSender { nullptr };

	struct qbufPipe_s *reportsPipe { nullptr };
	struct qthread_s *writerThread { nullptr };
	struct qthread_s *senderThread { nullptr };

	static const char *MakeLocalStoragePath();

	ReliablePipe();
	~ReliablePipe();

	void EnqueueMatchReport( QueryObject *matchReport );
};

#endif
