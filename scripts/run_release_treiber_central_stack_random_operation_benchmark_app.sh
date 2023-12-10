#PBS -l walltime=00:10:00
#PBS -l select=$($1):ncpus=1:mpiprocs=1:mem=1000m,place=free

echo "procNum: $1"
cd ../install-release/bin/ || exit

if [ ! -d "centralized" ]
then
  mkdir "centralized"
fi

cd "centralized" || exit

if [ ! -d "random_op" ]
then
  mkdir "random_op"
fi

cd "random_op" || exit

if [ -d $1 ]
then
  echo "cannot run the mpiexec because the directory $1 already exists"
  exit 1
fi

mkdir $1
cd $1 || exit
mpiexec -np "$1" ../../../rma_treiber_central_stack_random_operation_benchmark_app