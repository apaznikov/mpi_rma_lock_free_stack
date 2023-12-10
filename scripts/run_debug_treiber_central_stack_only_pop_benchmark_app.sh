#PBS -l walltime=00:10:00
#PBS -l select=$($1):ncpus=1:mpiprocs=1:mem=1000m,place=free

echo "procNum: $1"
cd ../install-debug/bin/ || exit

if [ ! -d "centralized" ]
then
  mkdir "centralized"
fi

cd "centralized" || exit

if [ ! -d "only_pop" ]
then
  mkdir "only_pop"
fi

cd "only_pop" || exit

if [ -d $1 ]
then
  echo "cannot run the mpiexec because the directory $1 already exists"
  exit 1
fi

mkdir $1
cd $1 || exit
mpiexec -np "$1" ../../../rma_treiber_central_stack_only_pop_benchmark_app