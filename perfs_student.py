#* ------------
#* This code is provided solely for the personal and private use of
#* students taking the CSC367 course at the University of Toronto.
#* Copying for purposes other than this use is expressly prohibited.
#* All forms of distribution of this code, whether as given or with
#* any changes, are expressly prohibited.
#*
#* Authors: Bogdan Simion, Maryam Dehnavi, Felipe de Azevedo Piovezan
#*
#* All of the files in this directory and all subdirectories are:
#* Copyright (c) 2020 Bogdan Simion and Maryam Dehnavi
#* -------------

#This script relies HEAVILY on the output provided by the starter_code.
#If you changed it somehow or added new printf statements (neither of which you
#should do for the final submission), the script won't work.


import subprocess
import os
import re
from collections import defaultdict
from textwrap import wrap
import pickle
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
import plotter

#executes a command and kills the python execution if
#said command fails.
def execute_command(command):
  print(">>>>> Executing command {}".format(command))
  process = subprocess.Popen(command, stdout = subprocess.PIPE,
      stderr = subprocess.STDOUT, shell=True,
      universal_newlines = True)
  return_code = process.wait()
  output = process.stdout.read()

  if return_code == 1:
    print("failed to execute command = ", command)
    print(output)
    exit()

  return output


def parse_perf(x):
  dict = {}
  dict['dump'] = x
  x = x.replace(',', '')
  # perf has this weird behavior where if we pass some counters with
  # "u", i.e., -e instructions:u,L1-dcache-loads:u
  # some of them will be reported without the u, which is
  # why it is removed here.
  items = {'seconds time elapsed' : 'time', #this is always recorded, but we're not using it.
       'instructions:u' : 'instructions',
       'L1-dcache-loads' : 'l1d_load',
       'L1-dcache-load-misses' : 'l1d_loadmisses',
       'LLC-loads' : 'll_load',
       'LLC-load-misses' : 'll_loadmisses',
       }
  #this     \/ whitespace is important to get the longest match.
  fp_regex = ('.*\s((\d+\.\d*)|(\d+))\s*{}\s*(#.*L1-dcache hits)?\s*' +
      re.escape('( +-') + '\s*(\d+\.\d+)' + re.escape('% )') + '.*')
  for name, key in items.items():
    vals = re.match(fp_regex.format(name), x, flags=re.DOTALL)
    if vals != None:
      dict[key] = vals[1]
  return dict

threads = [1,2,4,8]
chunk_sizes = [1, 2, 4, 8, 16, 32]
methods = {
       "sequential" : 1,
       "sharded_rows": 2,
       "sharded_columns column major" : 3,
       "sharded_columns row major" : 4,
       "work queue" : 5,
      }
filters = {"3x3" : 1,
       "5x5" : 2,
       "9x9" : 3,
       "1x1" : 4}

width_sizes = [1,8,16,64,512,1024,4096,32768]

#keys are tuples:
#(filter, method, numthreads, [chunk_size])
results = defaultdict()
#this will create a file called "data.pickle". If this
#exists when the program is run, perf will not be run again
#on the results that were saved in said file. To force
#a fresh run, delete data.pickle before running the script.
if os.path.isfile('data.pickle'):
  with open('data.pickle', 'rb') as f:
    results = pickle.load(f)

#This function runs perf using the built in square image by default.
default_file = '1'
def run_perf(filter, method, numthreads = 1, chunk_size = 1, input_file=default_file, repeat =
    10):
  key = (filter, method, numthreads, chunk_size, input_file)
  if results.get(key) != None:
    return

  main_args = './main.out -t {} -b {} -f {} -m {} -n {} -c {}'.format(
      0, #don't print time
      input_file,
      filters[filter],
      methods[method],
      numthreads,
      chunk_size)

  #cold run
  command =  'perf stat '
  command += main_args
  ret = execute_command(command)

  #actual run: captures at most 4 counters per perf execution
  # to avoid any side-effect from perf.
  counters = [
       'instructions:u',
       'L1-dcache-loads:u',
       'L1-dcache-load-misses:u',
       'LLC-loads:u',
       'LLC-load-misses:u',
       ]
  groups_of_four_counters = [counters[i:i+4] for i in
      range(0, len(counters), 4)]
  partial_results = {}
  for counter_group in groups_of_four_counters:
    command =  'perf stat -r {} -e {} '.format(repeat,
        ",".join(counter_group))
    command += main_args
    ret = execute_command(command)
    parsed = parse_perf(ret)
    partial_results = {**parsed, **partial_results}
    if partial_results['dump'] != parsed['dump']:
      partial_results['dump'] += '\n' + parsed['dump']

  #get time
  main_args = './main.out -t {} -b {} -f {} -m {} -n {} -c {}'.format(
      1, #print time
      input_file,
      filters[filter],
      methods[method],
      numthreads,
      chunk_size)

  time = 0
  for i in range(repeat):
    ret = execute_command(main_args)
    time += float(ret[5:])
  time = time / repeat
  partial_results['time'] = time

  results[key] = partial_results

  #saves the result to the pickle file.
  with open('data.pickle', 'wb') as f:
    pickle.dump(results, f, pickle.HIGHEST_PROTOCOL)


# helper for experiment 4
def run_perf_exp4(filter, method, numthreads = 1, chunk_size = 1, width = 1, repeat =
10):
    key = (filter, method, numthreads, chunk_size, width)
    if results.get(key) != None:
        return

    pgm_name = 'pgmWidthSize{}.txt'.format(width)

    main_args = './main.out -t {} -i {} -f {} -m {} -n {} -c {}'.format(
        0, #don't print time
        pgm_name,
        filters[filter],
        methods[method],
        numthreads,
        chunk_size)

    #cold run
    command =  'perf stat '
    command += main_args
    ret = execute_command(command)

    # #actual run: captures at most 4 counters per perf execution
    # # to avoid any side-effect from perf.
    # counters = [
    #     'instructions:u',
    #     'L1-dcache-loads:u',
    #     'L1-dcache-load-misses:u',
    #     'LLC-loads:u',
    #     'LLC-load-misses:u',
    # ]
    # groups_of_four_counters = [counters[i:i+4] for i in
    #                            range(0, len(counters), 4)]
    partial_results = {}
    # for counter_group in groups_of_four_counters:
    #     command =  'perf stat -r {} -e {} '.format(repeat,
    #                                                ",".join(counter_group))
    #     command += main_args
    #     ret = execute_command(command)
    #     parsed = parse_perf(ret)
    #     partial_results = {**parsed, **partial_results}
    #     if partial_results['dump'] != parsed['dump']:
    #         partial_results['dump'] += '\n' + parsed['dump']

    #get time
    main_args = './main.out -t {} -i {} -f {} -m {} -n {} -c {}'.format(
        1, #print time
        pgm_name,
        filters[filter],
        methods[method],
        numthreads,
        chunk_size)

    time = 0
    for i in range(repeat):
        ret = execute_command(main_args)
        time += float(ret[5:])
    time = time / repeat
    partial_results['time'] = time

    results[key] = partial_results

    #saves the result to the pickle file.
    with open('data.pickle', 'wb') as f:
        pickle.dump(results, f, pickle.HIGHEST_PROTOCOL)

colours = {"sequential" : 'r',
       "sharded_rows": 'b',
       "sharded_columns column major" : 'g',
       "sharded_columns row major" : 'c',
       "work queue" : 'm',
       1 : 'r',
       2 : 'b',
       3 : 'g',
       4 : 'm',
       8 : 'k',
       "1x1" : 'r',
       "3x3" : 'b',
       "5x5" : 'g',
       "9x9" : 'c',
      }

#Plot "mode" on the y axis and  number of threads on the x axis.
def graph(mode, filter = "3x3"):
  local_results = defaultdict(list)
  for method in methods:
    for nthread in threads:
      chunk_size = nthread
      key = (filter, method, nthread, chunk_size, default_file)
      run_perf(*key)
      local_results[method] += [float(results[key][mode])]

  title = ('4M pixels square image, filter = {}, chunk_size (workqueue) = #threads. Average over 10 runs.'.format(filter))
  ylabel = mode
  if mode == 'time':
    ylabel += "(s)"

  #sets are unordered by default, so impose an order with this list.
  methods_as_list = list(methods.keys())

  plotter.graph(
    threads, # x axis
    [local_results[method] for method in methods_as_list], # y vals
    methods_as_list,  # names for each line
    [colours[method] for method in methods_as_list],
    'graph_{}.png'.format(mode+filter+" 1"), # filename
    title,
    "# Threads", # xlabel
    ylabel
  )

# graph('time')
# graph('l1d_loadmisses')

def graph2(mode, filter = "3x3"):
  local_results = defaultdict(list)
  for nthread in threads:
    for chunk_size in chunk_sizes:
      method = "work queue"
      key = (filter, method, nthread, chunk_size, default_file)
      run_perf(*key)
      local_results[nthread] += [float(results[key][mode])]

  title = ("4M pixels square image, filter = {}, method = work queue."+
  " Average over 10 runs.".format(filter))
  ylabel = mode
  if mode == 'time':
    ylabel += "(s)"

  #sets are unordered by default, so impose an order with this list.
  methods_as_list = list(methods.keys())

  plotter.graph(
    chunk_sizes, # x axis
    [local_results[nthread] for nthread in threads], # y vals
    threads,  # names for each line
    [colours[nthread] for nthread in threads],
    'graph_{}.png'.format(mode+filter+" 2"), # filename
    title,
    "Chuck Size", # xlabel
    ylabel
  )

# graph2('time')
# graph2('l1d_loadmisses')

def graph3(mode):
  local_results = defaultdict(list)
  for method in methods:
    for filter in ["1x1", "3x3", "5x5", "9x9"]:
      nthread = 8
      chunk_size = nthread
      key = (filter, method, nthread, chunk_size, default_file)
      run_perf(*key)
      local_results[method] += [float(results[key][mode])]

  title = ('4M pixels square image, #thread = 8, chunk_size = 8. Average over 10 runs.')
  ylabel = mode
  if mode == 'time':
    ylabel += "(s)"

  #sets are unordered by default, so impose an order with this list.
  methods_as_list = list(methods.keys())
  filters_as_list = ["1x1", "3x3", "5x5", "9x9"]

  plotter.graph(
    filters_as_list, # x axis
    [local_results[method] for method in methods_as_list], # y vals
    methods_as_list,  # names for each line
    [colours[method] for method in methods_as_list],
    'graph_{}.png'.format(mode+" 3"), # filename
    title,
    "Filter Sizes", # xlabel
    ylabel
  )
  
#graph3('time')
#graph3('l1d_loadmisses')


def graph4(mode, filter = "3x3"):
    local_results = defaultdict(list)

    for method in methods:
        for width in width_sizes:
            nthread = 8
            chunk_size = nthread
            key = (filter, method, nthread, chunk_size, width)
            run_perf_exp4(*key)
            local_results[method] += [float(results[key][mode])]

    title = ('4M pixels square image, #thread = 8, chunk_size = 8. Average over 10 runs.')
    ylabel = mode
    if mode == 'time':
        ylabel += "(s)"

    #sets are unordered by default, so impose an order with this list.
    methods_as_list = list(methods.keys())

    plotter.graph(
        width_sizes, # x axis
        [local_results[method] for method in methods_as_list], # y vals
        methods_as_list,  # names for each line
        [colours[method] for method in methods_as_list],
        'graph_{}.png'.format(mode+" 4"), # filename
        title,
        "Width Sizes", # xlabel
        ylabel
    )

graph4('time')

