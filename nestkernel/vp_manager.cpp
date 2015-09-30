/*
 *  vp_manager.cpp
 *
 *  This file is part of NEST.
 *
 *  Copyright (C) 2004 The NEST Initiative
 *
 *  NEST is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  NEST is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with NEST.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "vp_manager.h"
#include "dictutils.h"
#include "network.h"

nest::VPManager::VPManager()
  : force_singlethreading_( false )
  , n_threads_( 1 )
{
}

void
nest::VPManager::init()
{
#ifndef _OPENMP
  if ( n_threads_ > 1 )
  {
    Network::get_network().message( SLIInterpreter::M_ERROR,
      "Network::reset",
      "No multithreading available, using single threading" );
    n_threads_ = 1;
    force_singlethreading_ = true;
  }
#endif

  set_num_threads( get_num_threads() );
}

void
nest::VPManager::reset()
{
  force_singlethreading_ = false;
  set_num_threads( 1 );
}

void
nest::VPManager::set_status( const Dictionary& d )
{
  long n_threads;
  bool n_threads_updated = updateValue< long >( d, "local_num_threads", n_threads );
  if ( n_threads_updated )
  {
    if ( Network::get_network().size() > 1 )
      throw KernelException( "Nodes exist: Thread/process number cannot be changed." );
    if ( Network::get_network().models_.size() > Network::get_network().pristine_models_.size() )
      throw KernelException(
        "Custom neuron models exist: Thread/process number cannot be changed." );
    if ( Network::get_network().connection_manager_.has_user_prototypes() )
      throw KernelException(
        "Custom synapse types exist: Thread/process number cannot be changed." );
    if ( Network::get_network().connection_manager_.get_user_set_delay_extrema() )
      throw KernelException(
        "Delay extrema have been set: Thread/process number cannot be changed." );
    if ( Network::get_network().get_simulated() )
      throw KernelException(
        "The network has been simulated: Thread/process number cannot be changed." );
    if ( not Time::resolution_is_default() )
      throw KernelException(
        "The resolution has been set: Thread/process number cannot be changed." );
    if ( Network::get_network().model_defaults_modified() )
      throw KernelException(
        "Model defaults have been modified: Thread/process number cannot be changed." );

    if ( n_threads > 1 && force_singlethreading_ )
    {
      Network::get_network().message( SLIInterpreter::M_WARNING,
        "Network::set_status",
        "No multithreading available, using single threading" );
      n_threads_ = 1;
    }

    // it is essential to call reset() here to adapt memory pools and more
    // to the new number of threads and VPs.
    n_threads_ = n_threads;
    reset();
  }

  long n_vps;
  bool n_vps_updated = updateValue< long >( d, "total_num_virtual_procs", n_vps );
  if ( n_vps_updated )
  {
    if ( Network::get_network().size() > 1 )
      throw KernelException( "Nodes exist: Thread/process number cannot be changed." );
    if ( Network::get_network().models_.size() > Network::get_network().pristine_models_.size() )
      throw KernelException(
        "Custom neuron models exist: Thread/process number cannot be changed." );
    if ( Network::get_network().connection_manager_.has_user_prototypes() )
      throw KernelException(
        "Custom synapse types exist: Thread/process number cannot be changed." );
    if ( Network::get_network().connection_manager_.get_user_set_delay_extrema() )
      throw KernelException(
        "Delay extrema have been set: Thread/process number cannot be changed." );
    if ( Network::get_network().get_simulated() )
      throw KernelException(
        "The network has been simulated: Thread/process number cannot be changed." );
    if ( not Time::resolution_is_default() )
      throw KernelException(
        "The resolution has been set: Thread/process number cannot be changed." );
    if ( Network::get_network().model_defaults_modified() )
      throw KernelException(
        "Model defaults have been modified: Thread/process number cannot be changed." );

    if ( n_vps % Communicator::get_num_processes() != 0 )
      throw BadProperty(
        "Number of virtual processes (threads*processes) must be an integer "
        "multiple of the number of processes. Value unchanged." );

    n_threads_ = n_vps / Communicator::get_num_processes();
    if ( ( n_threads > 1 ) && ( force_singlethreading_ ) )
    {
      Network::get_network().message( SLIInterpreter::M_WARNING,
        "Network::set_status",
        "No multithreading available, using single threading" );
      n_threads_ = 1;
    }

    // it is essential to call reset() here to adapt memory pools and more
    // to the new number of threads and VPs
    set_num_threads( n_threads_ );
    Network::get_network().reset();
  }
}

void
nest::VPManager::get_status( Dictionary& d )
{
  def< long >( d, "local_num_threads", n_threads_ );
  def< long >( d, "total_num_virtual_procs", kernel().vp_manager.get_num_virtual_processes() );
}

void
nest::VPManager::set_num_threads( nest::thread n_threads )
{
  n_threads_ = n_threads;
  Network::get_network().nodes_vec_.resize( n_threads_ );

#ifdef _OPENMP
  omp_set_num_threads( n_threads_ );

#ifdef USE_PMA
// initialize the memory pools
#ifdef IS_K
  assert( n_threads <= MAX_THREAD && "MAX_THREAD is a constant defined in allocator.h" );

#pragma omp parallel
  poormansallocpool[ omp_get_thread_num() ].init();
#else
#pragma omp parallel
  poormansallocpool.init();
#endif
#endif

#endif
  Communicator::set_num_threads( n_threads_ );
}

// TODO: put those functions as inlines in the header, as soon as all
//       references to Network are gone.

bool
nest::VPManager::is_local_vp( nest::thread vp ) const
{
  return Network::get_network().get_process_id( vp ) == Communicator::get_rank();
}

nest::thread
nest::VPManager::suggest_vp( nest::index gid ) const
{
  return gid % ( Network::get_network().n_sim_procs_ * n_threads_ );
}

nest::thread
nest::VPManager::suggest_rec_vp( nest::index gid ) const
{
  return gid % ( Network::get_network().n_rec_procs_ * n_threads_ )
    + Network::get_network().n_sim_procs_ * n_threads_;
}

nest::thread
nest::VPManager::vp_to_thread( nest::thread vp ) const
{
  if ( vp >= static_cast< thread >( Network::get_network().n_sim_procs_ * n_threads_ ) )
  {
    return ( vp + Network::get_network().n_sim_procs_ * ( 1 - n_threads_ )
             - Communicator::get_rank() ) / Network::get_network().n_rec_procs_;
  }
  else
  {
    return vp / Network::get_network().n_sim_procs_;
  }
}

nest::thread
nest::VPManager::thread_to_vp( nest::thread t ) const
{
  if ( Communicator::get_rank() >= static_cast< int >( Network::get_network().n_sim_procs_ ) )
  {
    // Rank is a recording process
    return t * Network::get_network().n_rec_procs_ + Communicator::get_rank()
      - Network::get_network().n_sim_procs_ + Network::get_network().n_sim_procs_ * n_threads_;
  }
  else
  {
    // Rank is a simulating process
    return t * Network::get_network().n_sim_procs_ + Communicator::get_rank();
  }
}
