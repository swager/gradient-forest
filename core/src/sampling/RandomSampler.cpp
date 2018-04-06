/*-------------------------------------------------------------------------------
  This file is part of generalized random forest (grf).

  grf is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  grf is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with grf. If not, see <http://www.gnu.org/licenses/>.
 #-------------------------------------------------------------------------------*/

#include <algorithm>
#include <unordered_map>

#include "RandomSampler.h"

RandomSampler::RandomSampler(uint seed,
                             const SamplingOptions& options):
    options(options) {
  random_number_generator.seed(seed);
}

void RandomSampler::sample_clusters(Data *data,
                                    double sample_fraction,
                                    std::vector<size_t> &samples) {
  if (options.clustering_enabled()) {
    size_t num_samples = options.get_num_clusters();
    sample(num_samples, sample_fraction, samples);
  } else {
    size_t num_samples = data->get_num_rows();
    sample(num_samples, sample_fraction, samples);
  }
}

void RandomSampler::sample(size_t num_samples,
                           double sample_fraction,
                           std::vector<size_t>& samples) {
  size_t num_samples_inbag = (size_t) num_samples * sample_fraction;
  if (options.get_sample_weights().empty()) {
    shuffle_and_split(samples, num_samples, num_samples_inbag);
  } else {
    draw_weighted(samples,
                  num_samples - 1,
                  num_samples_inbag,
                  options.get_sample_weights());
  }
}

void RandomSampler::subsample(const std::vector<size_t>& samples,
                              double sample_fraction,
                              std::vector<size_t>& subsamples) {
  std::vector<size_t> shuffled_sample(samples);
  std::shuffle(shuffled_sample.begin(), shuffled_sample.end(), random_number_generator);

  uint subsample_size = (uint) std::ceil(samples.size() * sample_fraction);
  subsamples.resize(subsample_size);
  std::copy(shuffled_sample.begin(),
            shuffled_sample.begin() + subsamples.size(),
            subsamples.begin());
}

void RandomSampler::subsample(const std::vector<size_t>& samples,
                              double sample_fraction,
                              std::vector<size_t>& subsamples,
                              std::vector<size_t>& oob_samples) {
  std::vector<size_t> shuffled_sample(samples);
  std::shuffle(shuffled_sample.begin(), shuffled_sample.end(), random_number_generator);

  uint subsample_size = (uint) std::ceil(samples.size() * sample_fraction);
  subsamples.resize(subsample_size);
  oob_samples.resize(samples.size() - subsample_size);

  std::copy(shuffled_sample.begin(),
            shuffled_sample.begin() + subsamples.size(),
            subsamples.begin());
  std::copy(shuffled_sample.begin() + subsamples.size(),
            shuffled_sample.end(),
            oob_samples.begin());
}

void RandomSampler::sample_from_clusters(const std::vector<size_t>& cluster_samples, std::vector<size_t>& samples) {
  // Now sample observations from clusters
  std::unordered_map<uint, std::vector<size_t>>& cluster_map = options.get_cluster_map();
  std::vector<size_t> cluster_obs_subsample;
  std::vector<size_t> dummy_cluster_obs_oob_subsample;

  for (auto const& cluster_id : cluster_samples) {
    std::vector<size_t> cluster_obs = cluster_map[cluster_id];
    double cluster_obs_sample_fraction = (double) options.get_samples_per_cluster() / cluster_obs.size();

    cluster_obs_subsample.clear();
    dummy_cluster_obs_oob_subsample.clear();
    RandomSampler::subsample(cluster_obs,
                             cluster_obs_sample_fraction,
                             cluster_obs_subsample,
                             dummy_cluster_obs_oob_subsample);
    samples.insert(samples.end(), cluster_obs_subsample.begin(), cluster_obs_subsample.end());
  }
}

void RandomSampler::shuffle_and_split(std::vector<size_t> &samples,
                                      size_t n_all,
                                      size_t size) {
  samples.resize(n_all);

  // Fill with 0..n_all-1 and shuffle
  std::iota(samples.begin(), samples.end(), 0);
  std::shuffle(samples.begin(), samples.end(), random_number_generator);

  samples.resize(size);
}

void RandomSampler::draw(std::vector<size_t>& result,
                         size_t max,
                         const std::set<size_t>& skip,
                         size_t num_samples) {
  if (num_samples < max / 2) {
    draw_simple(result, max, skip, num_samples);
  } else {
    draw_knuth(result, max, skip, num_samples);
  }
}

void RandomSampler::draw_simple(std::vector<size_t>& result,
                                size_t max,
                                const std::set<size_t>& skip,
                                size_t num_samples) {
  result.reserve(num_samples);

  // Set all to not selected
  std::vector<bool> temp;
  temp.resize(max, false);

  std::uniform_int_distribution<size_t> unif_dist(0, max - 1 - skip.size());
  for (size_t i = 0; i < num_samples; ++i) {
    size_t draw;
    do {
      draw = unif_dist(random_number_generator);
      for (auto& skip_value : skip) {
        if (draw >= skip_value) {
          ++draw;
        }
      }
    } while (temp[draw]);
    temp[draw] = true;
    result.push_back(draw);
  }
}

void RandomSampler::draw_knuth(std::vector<size_t> &result,
                               size_t max,
                               const std::set<size_t> &skip,
                               size_t num_samples) {
  size_t size_no_skip = max - skip.size();
  result.resize(num_samples);
  double u;
  size_t final_value;

  std::uniform_real_distribution<double> distribution(0.0, 1.0);

  size_t i = 0;
  size_t j = 0;
  while (i < num_samples) {
    u = distribution(random_number_generator);

    if ((size_no_skip - j) * u >= num_samples - i) {
      j++;
    } else {
      final_value = j;
      for (auto& skip_value : skip) {
        if (final_value >= skip_value) {
          ++final_value;
        }
      }
      result[i] = final_value;
      j++;
      i++;
    }
  }
}

void RandomSampler::draw_weighted(std::vector<size_t>& result,
                                  size_t max,
                                  size_t num_samples,
                                  const std::vector<double>& weights) {
  result.reserve(num_samples);

  // Set all to not selected
  std::vector<bool> temp;
  temp.resize(max + 1, false);

  std::discrete_distribution<> weighted_dist(weights.begin(), weights.end());
  for (size_t i = 0; i < num_samples; ++i) {
    size_t draw;
    do {
      draw = weighted_dist(random_number_generator);
    } while (temp[draw]);
    temp[draw] = true;
    result.push_back(draw);
  }
}

size_t RandomSampler::sample_poisson(size_t mean) {
  std::poisson_distribution<size_t> distribution(mean);
  return distribution(random_number_generator);
}

bool RandomSampler::clustering_enabled() const {
    return options.clustering_enabled();
}
