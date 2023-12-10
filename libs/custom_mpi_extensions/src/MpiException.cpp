//
// Created by denis on 02.02.23.
//

#include "include/MpiException.h"

namespace custom_mpi_extensions
{
    MpiException::MpiException(const char *t_info, const char *t_file, const char *t_functionName, int t_line,
                               int t_mpiStatus)
    :
    std::exception(),
    m_line(t_line),
    m_mpiStatus(t_mpiStatus),
    m_info(t_info),
    m_file(t_file),
    m_functionName(t_functionName)
    {

    }

    const char * MpiException::what () const noexcept
    {
        return "MpiException";
    }

    std::string_view MpiException::getInfo() const
    {
        return m_info;
    }
    std::string_view MpiException::getFile() const
    {
        return m_file;
    }
    int MpiException::getLine() const
    {
        return m_line;
    }

    int MpiException::getMpiStatus() const
    {
        return m_mpiStatus;
    }
}

std::ostream& operator<<(std::ostream& out, const custom_mpi_extensions::MpiException& ex)
{
    out << ex.what() << ": \'"
        << ex.getInfo() << "\' in file '" << ex.getFile() << "\' in line \'"
        << ex.getLine() << "\' with MPI Status \'" << ex.getMpiStatus() << "'\'\n";
    return out;
}