//
// Created by denis on 02.02.23.
//

#ifndef SOURCES_MPIEXCEPTION_H
#define SOURCES_MPIEXCEPTION_H


#include <string>
#include <exception>
#include <string_view>
#include <ostream>

namespace custom_mpi_extensions
{
    class MpiException: public std::exception
    {
    public:
        MpiException(const char *t_info, const char *t_file, const char *t_functionName, int t_line,
                     int t_mpiStatus);

        [[nodiscard]] const char * what () const noexcept final;

        [[nodiscard]] std::string_view getInfo() const;
        [[nodiscard]] std::string_view getFile() const;
        [[nodiscard]] int getLine() const;
        [[nodiscard]] int getMpiStatus() const;

    private:
        int m_line;
        int m_mpiStatus;
        std::string m_info;
        std::string m_file;
        std::string m_functionName;

    };
}

std::ostream& operator<<(std::ostream& out, const custom_mpi_extensions::MpiException& ex);
#endif //SOURCES_MPIEXCEPTION_H
