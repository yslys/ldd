```ssize_t``` is used for functions whose return value could either be a valid size, or a negative value to indicate an error. It is guaranteed to be able to store values at least in the range [-1, SSIZE_MAX] (SSIZE_MAX is system-dependent).

So you should use ```size_t``` whenever you mean to return a size in bytes, and ```ssize_t``` whenever you would return either a size in bytes or a (negative) error value.

Source:https://stackoverflow.com/questions/15739490/should-use-size-t-or-ssize-t
