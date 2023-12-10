#PBS -l walltime=00:10:00
#PBS -l select=$($1):ncpus=1:mpiprocs=1:mem=1000m,place=free

echo "procNum: $1"
cd ../install-debug/bin/ || exit

if [ ! -d "decentralized" ]
then
  mkdir "decentralized"
fi

cd "decentralized" || exit

if [ ! -d "only_push" ]
then
  mkdir "only_push"
fi

cd "only_push" || exit

if [ -d $1 ]
then
  echo "cannot run the mpiexec because the directory $1 already exists"
  exit 1
fi

mkdir $1
cd $1 || exit
mpiexec -np "$1" ../../../rma_treiber_decentralized_stack_only_push_benchmark_app