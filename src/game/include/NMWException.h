#ifndef NMWEXCEPTION_H
#define NMWEXCEPTION_H

#ifdef __cplusplus
extern "C" void abort(void);

namespace std {
typedef void (*terminate_handler)();
typedef void (*unexpected_handler)();
extern terminate_handler thandler;
extern unexpected_handler uhandler;
void dthandler();
void duhandler();
void terminate();
void unexpected();
} // namespace std
#endif

#endif
