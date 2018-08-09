#ifndef UI_CEF_ANIM_FRAMES_PARSER_H
#define UI_CEF_ANIM_FRAMES_PARSER_H

#include "ObjectFieldsGetter.h"
#include "../ViewAnimator.h"

/**
 * A helper for parsing and validation view anim frames from a JS array
 */
class BaseViewAnimParser {
	CefRefPtr<CefV8Value> &framesArray;
	const CefString &scope;
	const ViewAnimFrame *prevFrame { nullptr };

	bool ValidateNewFrame( const ViewAnimFrame *newFrame, int index, CefString &exception );
	bool ValidateLoopingAnim( const ViewAnimFrame *firstFrame, const ViewAnimFrame *lastFrame, CefString &exception );
protected:
	virtual ViewAnimFrame *ParseFrame( ObjectFieldsGetter &getter, int index, CefString &exception, const CefString &scope );

	/**
	 * Allocates a new frame object that is writable.
	 * Appends it to the frames list.
	 * @note the frame address is only guaranteed to be valid for the current ParseFrame() step.
	 */
	virtual ViewAnimFrame *AllocNextFrame() = 0;
	/**
	 * Returns the first frame in the parsed frames list (if any).
	 * @note the frame address might change after next {@code AllocNextFrame()} calls.
	 */
	virtual ViewAnimFrame *FirstFrame() = 0;
	/**
	 * Returns the last frame in the parsed frames list (if any).
	 * @note the frame address might change after next {@code AllocNextFrame()} calls.
	 */
	virtual ViewAnimFrame *LastFrame() = 0;
public:
	BaseViewAnimParser( CefRefPtr<CefV8Value> &framesArray_, const CefString &scope_ )
		: framesArray( framesArray_ ), scope( scope_ ) {}

	bool Parse( bool expectLooping, CefString &exception );

	static CefRefPtr<CefV8Value> FindAnimField( ObjectFieldsGetter &paramsGetter,
												const CefString &seqFieldName,
												const CefString &loopFieldName,
												const CefString **animFieldName,
												CefString &exception );
};

template <typename FrameImpl>
class ViewAnimParserImpl: public BaseViewAnimParser {
	std::vector<FrameImpl> framesStorage;

protected:
	ViewAnimFrame *AllocNextFrame() final {
		// Pushing an item first is important... data() returns null for an empty container
		framesStorage.emplace_back( FrameImpl() );
		return framesStorage.data() + framesStorage.size() - 1;
	}

	ViewAnimFrame *FirstFrame() final {
		return framesStorage.data();
	}

	ViewAnimFrame *LastFrame() final {
		return !framesStorage.empty() ? framesStorage.data() + framesStorage.size() - 1 : nullptr;
	}
public:
	ViewAnimParserImpl( CefRefPtr<CefV8Value> &framesArray_, const CefString &scope_ )
		: BaseViewAnimParser( framesArray_, scope_ ) {}

	const std::vector<FrameImpl> &Frames() const { return framesStorage; }
};

class ViewAnimParser final: public ViewAnimParserImpl<ViewAnimFrame> {
public:
	ViewAnimParser( CefRefPtr<CefV8Value> &framesArray_, const CefString &scope_ )
		: ViewAnimParserImpl( framesArray_, scope_ ) {}
};

class CameraAnimParser final: public ViewAnimParserImpl<CameraAnimFrame> {
	ViewAnimFrame *ParseFrame( ObjectFieldsGetter &fieldsGetter,
							   int index,
							   CefString &exception,
							   const CefString &frameScope ) override;
public:
	CameraAnimParser( CefRefPtr<CefV8Value> &framesArray_, const CefString &scope_ )
		: ViewAnimParserImpl( framesArray_, scope_ ) {}
};

#endif
