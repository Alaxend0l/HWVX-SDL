#ifndef _EXCEPTION_H
#define _EXCEPTION_H

namespace std {

class exception {
public:
    const char *what() const;
};

class bad_alloc : public exception {
public:
    const char *what() const;
};

class bad_exception : public exception {
public:
    const char *what() const;
};

}

#endif
