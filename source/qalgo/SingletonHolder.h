#ifndef QFUSION_SINGLETONHOLDER_H
#define QFUSION_SINGLETONHOLDER_H

#include <memory>
#include <stdint.h>
#include <assert.h>

/**
 * A helper for holding singleton instances that behave like any regular RAII-using class.
 * {@code Init()} and {@code Shutdown()} are expected to be called manually.
 * This is due to the existing lifecycle pattern of dynamic libraries.
 * @tparam T a type of a held instance.
 * @note The held object constructor and destructor are often intended to be private.
 * The line {@code template<typename> friend class SingletonHolder}
 * is proposed to be added to such classes. This approach is intrusive
 * but switching from non-structured ways of handling own instances
 * to these holders is intrusive too.
 */
template <class T>
class alignas( 8 )SingletonHolder {
protected:
	uint8_t buffer[sizeof( T )];
	T *instance { nullptr };
public:
	/**
	 * Must be called manually.
	 * Calling it repeatedly without making a {@code Shutdown()} call in-between is illegal.
	 * Forwards its arguments list to a constructor of the held type.
	 */
	template <typename... Args>
	void Init( Args... args ) {
		assert( !instance );
		// Check whether the buffer is aligned properly
		assert( !( ( (uintptr_t)&instance[0] ) % 8 ) );
		instance = new( buffer )T( args... );
	}

	/**
	 * Returns a held instance that is assumed to be constructed (and not destroyed).
	 * A user is forced to care about the instance lifetime, and that is a good thing in this case.
	 * Usually held objects are really heavy-weight and important for the program logic,
	 * so a user has to care about initialization order anyway.
	 * @note the holder is supposed to be global, and accessing global variables
	 * in dynamic libraries involves indirection.
	 * A proposed usage pattern is getting an instance once and saving in a local variable
	 * if the held instance is used involved in performance-demanding code.
	 */
	T *Instance() {
		assert( instance );
		return instance;
	}

	/**
	 * Must be called manually.
	 * Calling it repeatedly without a prior {@code Init()} call is legal.
	 * Once this method is called, {@code Init()} is allowed to be called again.
	 */
	void Shutdown() {
		if( instance ) {
			instance->~T();
			instance = nullptr;
		}
	}
};

#endif
