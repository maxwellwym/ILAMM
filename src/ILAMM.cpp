# include <RcppArmadillo.h>
# include <iostream>
# include <string>
// [[Rcpp::depends(RcppArmadillo)]]

// [[Rcpp::export]]
int sgn(const double x) {
  return (x > 0) - (x < 0);
}

// [[Rcpp::export]]
arma::vec softThresh(const arma::vec& x, const arma::vec& lambda) {
  return sign(x) % max(abs(x) - lambda, arma::zeros(x.size()));
}

// [[Rcpp::export]]
arma::vec cmptLambda(const arma::vec& beta, const double lambda, const std::string penalty) {
  arma::vec rst = arma::zeros(beta.size());
  if (penalty == "Lasso") {
    rst = lambda * arma::ones(beta.size());
    rst(0) = 0;
  } else if (penalty == "SCAD") {
    double a = 3.7;
    for (int i = 1; i < beta.size(); i++) {
      double abBeta = std::abs(beta(i));
      if (abBeta <= lambda) {
        rst(i) = lambda;
      } else if (abBeta <= a * lambda) {
        rst(i) = (a * lambda - abBeta) / (a - 1);
      }
    }
  } else if (penalty == "MCP") {
    double a = 3;
    for (int i = 1; i < beta.size(); i++) {
      double abBeta = std::abs(beta(i));
      if (abBeta <= a * lambda) {
        rst(i) = lambda - abBeta / a;
      }
    }
  }
  return rst;
}

// [[Rcpp::export]]
double loss(const arma::vec& Y, const arma::vec& Ynew, const std::string lossType,
            const double tau) {
  double rst = 0;
  if (lossType == "l2") {
    rst = mean(square(Y - Ynew)) / 2;
  } else if (lossType == "Huber") {
    arma::vec res = Y - Ynew;
    for (int i = 0; i < Y.size(); i++) {
      if (std::abs(res(i)) <= tau) {
        rst += res(i) * res(i) / 2;
      } else {
        rst += tau * std::abs(res(i)) - tau * tau / 2;
      }
    }
    rst /= Y.size();
  }
  return rst;
}

// [[Rcpp::export]]
arma::vec gradLoss(const arma::mat& X, const arma::vec& Y, const arma::vec& beta,
                   const std::string lossType, const double tau, const bool interecept) {
  arma::vec res = Y - X * beta;
  arma::vec rst = arma::zeros(beta.size());
  if (lossType == "l2") {
    rst = -1 * (res.t() * X).t();
  } else if (lossType == "Huber") {
    for (int i = 0; i < Y.size(); i++) {
      if (std::abs(res(i)) <= tau) {
        rst -= res(i) * X.row(i).t();
      } else {
        rst -= tau * sgn(res(i)) * X.row(i).t();
      }
    }
  }
  if (!interecept) {
    rst(0) = 0;
  }
  return rst / Y.size();
}

// [[Rcpp::export]]
arma::vec updateBeta(const arma::mat& X, const arma::vec& Y, arma::vec beta, const double phi,
                     const arma::vec& Lambda, const std::string lossType, const double tau,
                     const bool intercept) {
  arma::vec first = beta - gradLoss(X, Y, beta, lossType, tau, intercept) / phi;
  arma::vec second = Lambda / phi;
  return softThresh(first, second);
}

// [[Rcpp::export]]
double cmptF(const arma::mat& X, const arma::vec& Y, const arma::vec& betaNew,
             const std::string lossType, const double tau) {
  return loss(Y, X * betaNew, lossType, tau);
}

// [[Rcpp::export]]
double cmptPsi(const arma::mat& X, const arma::vec& Y, const arma::vec& betaNew,
               const arma::vec& beta, const double phi, const std::string lossType,
               const double tau, const bool intercept) {
  arma::vec diff = betaNew - beta;
  double rst = loss(Y, X * beta, lossType, tau)
    + as_scalar((gradLoss(X, Y, beta, lossType, tau, intercept)).t() * diff)
    + phi * as_scalar(diff.t() * diff) / 2;
  return rst;
}

// [[Rcpp::export]]
Rcpp::List LAMM(const arma::mat& X, const arma::vec& Y, const arma::vec& Lambda, arma::vec beta,
                const double phi, const std::string lossType, const double tau,
                const double gamma, const bool interecept) {
  double phiNew = phi;
  arma::vec betaNew = arma::vec();
  while (true) {
    betaNew = updateBeta(X, Y, beta, phiNew, Lambda, lossType, tau, interecept);
    double FVal = cmptF(X, Y, betaNew, lossType, tau);
    double PsiVal = cmptPsi(X, Y, betaNew, beta, phiNew, lossType, tau, interecept);
    if (FVal <= PsiVal) {
      break;
    }
    phiNew *= gamma;
  }
  return Rcpp::List::create(Rcpp::Named("beta") = betaNew, Rcpp::Named("phi") = phiNew);
}

//' The function fits (high-dimensional) regularized regression with non-convex penalties: Lasso, SCAD and MCP, and it's implemented by I-LAMM algorithm.
//'
//' The design matrix \eqn{X} can be either high-dimensional or low-dimensional, its number of rows should be the same as the length of \eqn{Y}. Tunning parameter \eqn{\lambda} has a default setting but it can be user-specified. All the arguments except for \eqn{X} and \eqn{Y} have default settings.
//'
//' @title Non-convex regularized regression
//' @param X An \eqn{n} by \eqn{d} design matrix with each row being a sample and each column being a variable, either low-dimensional data (\eqn{d \le n}) or high-dimensional data (\eqn{d > n}) are allowed.
//' @param Y A continuous response vector with length \eqn{n}.
//' @param lambda Tuning parameter of regularized regression, its specified value should be positive. The default value is determined in this way: define \eqn{\lambda_max = max(|Y^T X|) / n}, and \eqn{\lambda_min = 0.01 * \lambda_max}, then \eqn{\lambda = exp(0.7 * log(\lambda_max) + 0.3 * log(\lambda_min))}.
//' @param penalty Type of non-convex penalties with default setting "SCAD", possible choices are: "Lasso", "SCAD" and "MCP".
//' @param phi0 The initial value of the isotropic parameter \eqn{\phi} in I-LAMM algorithm. The defalut value is 0.001.
//' @param gamma The inflation parameter in I-LAMM algorithm, in each iteration of I-LAMM, we will inflate \eqn{\phi} by \eqn{\gamma}. The defalut value is 1.5.
//' @param epsilon_c The tolerance level for contraction stage, iteration of contraction will stop when \eqn{||\beta_new - \beta_old||_2 / \sqrt(d + 1) < \epsilon_c}. The defalut value is 1e-4.
//' @param epsilon_t The tolerance level for tightening stage, iteration of tightening will stop when \eqn{||\beta_new - \beta_old||_2 / \sqrt(d + 1) < \epsilon_t}. The defalut value is 1e-4.
//' @param iteMax The maximal number of iteration in either contraction or tightening stage, if this number is reached, the convergence of I-LAMM is failed. The defalut value is 500.
//' @param intercept Boolean value indicating whether an intercept term should be included into the model. The default setting is \code{FALSE}.
//' @param itcpIncluded Boolean value indicating whether a column of 1's has been included in the design matrix \eqn{X}. The default setting is \code{FALSE}.
//' @return A list including the following terms will be returned:
//' \itemize{
//' \item \code{beta} The estimated \eqn{\beta}, a vector with length d + 1, with the first one being the value of intercept (0 if \code{intercept = FALSE}).
//' \item \code{phi} The final value of the isotropic parameter \eqn{\phi} in the last iteration of I-LAMM algorithm.
//' \item \code{penalty} The type of penalty.
//' \item \code{lambda} The value of \eqn{\lambda}.
//' \item \code{IteTightening} The number of tightenings in I-LAMM algorithm, and it's 0 if \code{penalty = "Lasso"}.
//' }
//' @author Xiaoou Pan, Qiang Sun, Wen-Xin Zhou
//' @references Fan, J., Liu, H., Sun, Q. and Zhang, T. (2018). I-LAMM for sparse learning: Simultaneous control of algorithmic complexity and statistical error. Ann. Statist. 46 814–841.
//' @seealso \code{\link{cvNcvxReg}}
//' @examples
//' n = 50
//' d = 100
//' set.seed(2018)
//' X = matrix(rnorm(n * d), n, d)
//' beta = c(rep(2, 3), rep(0, d - 3))
//' Y = X %*% beta + rnorm(n)
//' # Fit SCAD without intercept
//' fit = ncvxReg(X, Y)
//' fit$beta
//' # Fit MCP with intercept
//' fit = ncvxReg(X, Y, penalty = "MCP", intercept = TRUE)
//' fit$beta
//' @export
// [[Rcpp::export]]
Rcpp::List ncvxReg(arma::mat X, const arma::vec& Y, double lambda = -1,
                   std::string penalty = "SCAD", const double phi0 = 0.001,
                   const double gamma = 1.5, const double epsilon_c = 0.0001,
                   const double epsilon_t = 0.0001, const int iteMax = 500,
                   const bool intercept = false, const bool itcpIncluded = false) {
  if (!itcpIncluded) {
    arma::mat XX = arma::ones(X.n_rows, X.n_cols + 1);
    XX.cols(1, X.n_cols) = X;
    X = XX;
  }
  int n = Y.size();
  int d = X.n_cols - 1;
  if (lambda <= 0) {
    double lambdaMax = max(abs(Y.t() * X)) / n;
    double lambdaMin = 0.01 * lambdaMax;
    lambda = std::exp((long double)(0.7 * std::log((long double)lambdaMax)
                                      + 0.3 * std::log((long double)lambdaMin)));
  }
  arma::vec beta = arma::zeros(d + 1);
  arma::vec betaNew = arma::zeros(d + 1);
  // Contraction
  arma::vec Lambda = cmptLambda(beta, lambda, penalty);
  double phi = phi0;
  int ite = 0;
  while (ite <= iteMax) {
    ite++;
    Rcpp::List listLAMM = LAMM(X, Y, Lambda, beta, phi, "l2", 1, gamma, intercept);
    betaNew = Rcpp::as<arma::vec>(listLAMM["beta"]);
    phi = listLAMM["phi"];
    phi = std::max(phi0, phi / gamma);
    if (norm(betaNew - beta, 2) / std::sqrt(d + 1) <= epsilon_c) {
      break;
    }
    beta = betaNew;
  }
  int iteT = 0;
  // Tightening
  if (penalty != "Lasso") {
    arma::vec beta0 = arma::zeros(d + 1);
    while (iteT <= iteMax) {
      iteT++;
      beta = betaNew;
      beta0 = betaNew;
      Lambda = cmptLambda(beta, lambda, penalty);
      phi = phi0;
      ite = 0;
      while (ite <= iteMax) {
        ite++;
        Rcpp::List listLAMM  = LAMM(X, Y, Lambda, beta, phi, "l2", 1, gamma, intercept);
        betaNew = Rcpp::as<arma::vec>(listLAMM["beta"]);
        phi = listLAMM["phi"];
        phi = std::max(phi0, phi / gamma);
        if (norm(betaNew - beta, 2) / std::sqrt(d + 1) <= epsilon_t) {
          break;
        }
        beta = betaNew;
      }
      if (norm(betaNew - beta0, 2) / std::sqrt(d + 1) <= epsilon_t) {
        break;
      }
    }
  }
  return Rcpp::List::create(Rcpp::Named("beta") = betaNew, Rcpp::Named("phi") = phi,
                            Rcpp::Named("penalty") = penalty, Rcpp::Named("lambda") = lambda,
                            Rcpp::Named("IteTightening") = iteT);
}

//' The function fits (high-dimensional) Huber regularized regression with non-convex penalties: Lasso, SCAD and MCP, and it's implemented by I-LAMM algorithm.
//'
//' The design matrix \eqn{X} can be either high-dimensional or low-dimensional, its number of rows should be the same as the length of \eqn{Y}. Tunning parameters \eqn{\lambda} and \eqn{\tau} have default settings but they can be user-specified. All the arguments except for \eqn{X} and \eqn{Y} have default settings.
//'
//' @title Non-convex regularized Huber regression
//' @param X An \eqn{n} by \eqn{d} design matrix with each row being a sample and each column being a variable, either low-dimensional data (\eqn{d \le n}) or high-dimensional data (\eqn{d > n}) are allowed.
//' @param Y A continuous response vector with length \eqn{n}.
//' @param lambda Tuning parameter of regularized regression, its specified value should be positive. The default value is determined in this way: define \eqn{\lambda_max = max(|Y^T X|) / n}, and \eqn{\lambda_min = 0.01 * \lambda_max}, then \eqn{\lambda = exp(0.7 * log(\lambda_max) + 0.3 * log(\lambda_min))}.
//' @param penalty Type of non-convex penalties with default setting "SCAD", possible choices are: "Lasso", "SCAD" and "MCP".
//' @param tau Robustness parameter of Huber loss function, its specified value should be positive. The default value is determined in this way: define \eqn{R} as the residual from Lasso by fitting \code{ncvxReg} with \code{lambda}, and \eqn{\sigma_MAD = median(|R - median(R)|) / \Phi^(-1)(3/4)} is the median absolute deviation estimator, then \eqn{\tau = \sigma_MAD \sqrt(n / log(nd))}.
//' @param phi0 The initial value of the isotropic parameter \eqn{\phi} in I-LAMM algorithm. The defalut value is 0.001.
//' @param gamma The inflation parameter in I-LAMM algorithm, in each iteration of I-LAMM, we will inflate \eqn{\phi} by \eqn{\gamma}. The defalut value is 1.5.
//' @param epsilon_c The tolerance level for contraction stage, iteration of contraction will stop when \eqn{||\beta_new - \beta_old||_2 / \sqrt(d + 1) < \epsilon_c}. The defalut value is 1e-4.
//' @param epsilon_t The tolerance level for tightening stage, iteration of tightening will stop when \eqn{||\beta_new - \beta_old||_2 / \sqrt(d + 1) < \epsilon_t}. The defalut value is 1e-4.
//' @param iteMax The maximal number of iteration in either contraction or tightening stage, if this number is reached, the convergence of I-LAMM is failed. The defalut value is 500.
//' @param intercept Boolean value indicating whether an intercept term should be included into the model. The default setting is \code{FALSE}.
//' @param itcpIncluded Boolean value indicating whether a column of 1's has been included in the design matrix \eqn{X}. The default setting is \code{FALSE}.
//' @return A list including the following terms will be returned:
//' \itemize{
//' \item \code{beta} The estimated \eqn{\beta}, a vector with length d + 1, with the first one being the value of intercept (0 if \code{intercept = FALSE}).
//' \item \code{phi} The final value of the isotropic parameter \eqn{\phi} in the last iteration of I-LAMM algorithm.
//' \item \code{penalty} The type of penalty.
//' \item \code{lambda} The value of \eqn{\lambda}.
//' \item \code{tau} The value of \eqn{\tau}.
//' \item \code{IteTightening} The number of tightenings in I-LAMM algorithm, and it's 0 if \code{penalty = "Lasso"}.
//' }
//' @author Xiaoou Pan, Qiang Sun, Wen-Xin Zhou
//' @references Fan, J., Liu, H., Sun, Q. and Zhang, T. (2018). I-LAMM for sparse learning: Simultaneous control of algorithmic complexity and statistical error. Ann. Statist. 46 814–841.
//' @seealso \code{\link{cvNcvxHuberReg}}
//' @examples
//' n = 50
//' d = 100
//' set.seed(2018)
//' X = matrix(rnorm(n * d), n, d)
//' beta = c(rep(2, 3), rep(0, d - 3))
//' Y = X %*% beta + rlnorm(n, 0, 1.2) - exp(1.2^2 / 2)
//' # Fit Huber-SCAD without intercept
//' fit = ncvxHuberReg(X, Y)
//' fit$beta
//' # Fit Huber-MCP with intercept
//' fit = ncvxHuberReg(X, Y, penalty = "MCP", intercept = TRUE)
//' fit$beta
//' @export
// [[Rcpp::export]]
Rcpp::List ncvxHuberReg(arma::mat X, const arma::vec& Y, double lambda = -1,
                std::string penalty = "SCAD", double tau = -1, const double phi0 = 0.001,
                const double gamma = 1.5, const double epsilon_c = 0.0001,
                const double epsilon_t = 0.0001, const int iteMax = 500,
                const bool intercept = false, const bool itcpIncluded = false) {
  if (!itcpIncluded) {
    arma::mat XX = arma::ones(X.n_rows, X.n_cols + 1);
    XX.cols(1, X.n_cols) = X;
    X = XX;
  }
  int n = Y.size();
  int d = X.n_cols - 1;
  if (lambda <= 0) {
    double lambdaMax = max(abs(Y.t() * X)) / n;
    double lambdaMin = 0.01 * lambdaMax;
    lambda = std::exp((long double)(0.7 * std::log((long double)lambdaMax)
                      + 0.3 * std::log((long double)lambdaMin)));
  }
  if (tau <= 0) {
    Rcpp::List listILAMM = ncvxReg(X, Y, lambda, "Lasso", phi0, gamma, epsilon_c, epsilon_t,
                                   iteMax, intercept, true);
    arma::vec betaLasso = Rcpp::as<arma::vec>(listILAMM["beta"]);
    arma::vec Yhat = X * betaLasso;
    arma::vec res = Y - Yhat;
    double sigmaHat = median(abs(res - median(res))) / 0.6745;
    tau = sigmaHat * std::sqrt((long double)(n / std::log(n * d)));
  }
  arma::vec beta = arma::zeros(d + 1);
  arma::vec betaNew = arma::zeros(d + 1);
  // Contraction
  arma::vec Lambda = cmptLambda(beta, lambda, penalty);
  double phi = phi0;
  int ite = 0;
  while (ite <= iteMax) {
    ite++;
    Rcpp::List listLAMM = LAMM(X, Y, Lambda, beta, phi, "Huber", tau, gamma, intercept);
    betaNew = Rcpp::as<arma::vec>(listLAMM["beta"]);
    phi = listLAMM["phi"];
    phi = std::max(phi0, phi / gamma);
    if (norm(betaNew - beta, 2) / std::sqrt(d + 1) <= epsilon_c) {
      break;
    }
    beta = betaNew;
  }
  int iteT = 0;
  // Tightening
  if (penalty != "Lasso") {
    arma::vec beta0 = arma::zeros(d + 1);
    while (iteT <= iteMax) {
      iteT++;
      beta = betaNew;
      beta0 = betaNew;
      Lambda = cmptLambda(beta, lambda, penalty);
      phi = phi0;
      ite = 0;
      while (ite <= iteMax) {
        ite++;
        Rcpp::List listLAMM  = LAMM(X, Y, Lambda, beta, phi, "Huber", tau, gamma, intercept);
        betaNew = Rcpp::as<arma::vec>(listLAMM["beta"]);
        phi = listLAMM["phi"];
        phi = std::max(phi0, phi / gamma);
        if (norm(betaNew - beta, 2) / std::sqrt(d + 1) <= epsilon_t) {
          break;
        }
        beta = betaNew;
      }
      if (norm(betaNew - beta0, 2) / std::sqrt(d + 1) <= epsilon_t) {
        break;
      }
    }
  }
  return Rcpp::List::create(Rcpp::Named("beta") = betaNew, Rcpp::Named("phi") = phi,
                            Rcpp::Named("penalty") = penalty, Rcpp::Named("lambda") = lambda,
                            Rcpp::Named("tau") = tau, Rcpp::Named("IteTightening") = iteT);
}

// [[Rcpp::export]]
arma::uvec getIndex(const int n, const int low, const int up) {
  arma::vec seq = arma::regspace(0, n - 1);
  return arma::find(seq >= low && seq <= up);
}

// [[Rcpp::export]]
arma::uvec getIndexComp(const int n, const int low, const int up) {
  arma::vec seq = arma::regspace(0, n - 1);
  return arma::find(seq < low || seq > up);
}

// [[Rcpp::export]]
arma::vec tauConst(int n) {
  int end = n >> 1;
  int start = (n == end << 1) ? (end - 1) : end;
  arma::vec rst = arma::vec(n);
  int j = 0;
  for (int i = start; i > 0; i--) {
    rst(j++) = (double)1 / (1 << i);
  }
  for (int i = 0; i <= end; i++) {
    rst(j++) = 1 << i;
  }
  return rst;
}

//' The function performs k-fold cross validation for (high-dimensional) regularized regression with non-convex penalties: Lasso, SCAD and MCP, and it's implemented by I-LAMM algorithm.
//'
//' The design matrix \eqn{X} can be either high-dimensional or low-dimensional, its number of rows should be the same as the length of \eqn{Y}. The sequence of \eqn{\lambda}'s has a default setting but it can be user-specified. All the arguments except for \eqn{X} and \eqn{Y} have default settings.
//'
//' @title K-fold cross validation for non-convex regularized regression
//' @param X An \eqn{n} by \eqn{d} design matrix with each row being a sample and each column being a variable, either low-dimensional data (\eqn{d \le n}) or high-dimensional data (\eqn{d > n}) are allowed.
//' @param Y A continuous response vector with length \eqn{n}.
//' @param lSeq Sequence of tuning parameter of regularized regression \eqn{\lambda}, every element should be positive. If it's not specified, the default sequence is generated in this way: define \eqn{\lambda_max = max(|Y^T X|) / n}, and \eqn{\lambda_min = 0.01 * \lambda_max}, then \code{lseq} is a sequence from \eqn{\lambda_max} to \eqn{\lambda_min} that decreases uniformly on log scale.
//' @param nlambda Number of \eqn{\lambda} to generate the default sequence \code{lSeq}. It's not necessary if \code{lSeq} is specified. The default value is 30.
//' @param penalty Type of non-convex penalties with default setting "SCAD", possible choices are: "Lasso", "SCAD" and "MCP".
//' @param phi0 The initial value of the isotropic parameter \eqn{\phi} in I-LAMM algorithm. The defalut value is 0.001.
//' @param gamma The inflation parameter in I-LAMM algorithm, in each iteration of I-LAMM, we will inflate \eqn{\phi} by \eqn{\gamma}. The defalut value is 1.5.
//' @param epsilon_c The tolerance level for contraction stage, iteration of contraction will stop when \eqn{||\beta_new - \beta_old||_2 / \sqrt(d + 1) < \epsilon_c}. The defalut value is 1e-4.
//' @param epsilon_t The tolerance level for tightening stage, iteration of tightening will stop when \eqn{||\beta_new - \beta_old||_2 / \sqrt(d + 1) < \epsilon_t}. The defalut value is 1e-4.
//' @param iteMax The maximal number of iteration in either contraction or tightening stage, if this number is reached, the convergence of I-LAMM is failed. The defalut value is 500.
//' @param nfolds The number of folds to conduct cross validation, values that are greater than 10 are not recommended, and it'll be modified to 10 if the input is greater than 10. The default value is 3.
//' @param intercept Boolean value indicating whether an intercept term should be included into the model. The default setting is \code{FALSE}.
//' @param itcpIncluded Boolean value indicating whether a column of 1's has been included in the design matrix \eqn{X}. The default setting is \code{FALSE}.
//' @return A list including the following terms will be returned:
//' \itemize{
//' \item \code{beta} The estimated \eqn{\beta} with \eqn{\lambda} determined by cross validation, it's a vector with length d + 1, with the first one being the value of intercept (0 if \code{intercept = FALSE}).
//' \item \code{penalty} The type of penalty.
//' \item \code{lambdaSeq} The sequence of \eqn{\lambda}'s for cross validation.
//' \item \code{mse} The mean squared error from cross validation, it's a vector with length \code{nlambda}.
//' \item \code{lambdaMin} The value of \eqn{\lambda} in \code{lambdaSeq} that minimized \code{mse}.
//' \item \code{nfolds} The number of folds for cross validation.
//' }
//' @author Xiaoou Pan, Qiang Sun, Wen-Xin Zhou
//' @references Fan, J., Liu, H., Sun, Q. and Zhang, T. (2018). I-LAMM for sparse learning: Simultaneous control of algorithmic complexity and statistical error. Ann. Statist. 46 814–841.
//' @seealso \code{\link{ncvxReg}}
//' @examples
//' n = 50
//' d = 100
//' set.seed(2018)
//' X = matrix(rnorm(n * d), n, d)
//' beta = c(rep(2, 3), rep(0, d - 3))
//' Y = X %*% beta + rnorm(n)
//' # Fit SCAD without intercept, with lambda determined by 3-folds cross validation
//' fit = cvNcvxReg(X, Y)
//' fit$beta
//' fit$lambdaMin
//' # Fit MCP with intercept, with lambda determined by 5-folds cross validation
//' fit = cvNcvxReg(X, Y, penalty = "MCP", intercept = TRUE, nfolds = 5)
//' fit$beta
//' fit$lambdaMin
//' @export
// [[Rcpp::export]]
Rcpp::List cvNcvxReg(arma::mat& X, const arma::vec& Y,
                    Rcpp::Nullable<Rcpp::NumericVector> lSeq = R_NilValue, int nlambda = 30,
                    const std::string penalty = "SCAD", const double phi0 = 0.001,
                    const double gamma = 1.5, const double epsilon_c = 0.0001,
                    const double epsilon_t = 0.0001, const int iteMax = 500, int nfolds = 3,
                    const bool intercept = false, const bool itcpIncluded = false) {
  if (!itcpIncluded) {
    arma::mat XX = arma::ones(X.n_rows, X.n_cols + 1);
    XX.cols(1, X.n_cols) = X;
    X = XX;
  }
  int n = Y.size();
  arma::vec lambdaSeq = arma::vec();
  if (lSeq.isNotNull()) {
    lambdaSeq = Rcpp::as<arma::vec>(lSeq);
    nlambda = lambdaSeq.size();
  } else {
    double lambdaMax = max(abs(Y.t() * X)) / n;
    double lambdaMin = 0.01 * lambdaMax;
    lambdaSeq = exp(arma::linspace(std::log((long double)lambdaMin),
                                   std::log((long double)lambdaMax), nlambda));
  }
  if (nfolds > 10 || nfolds > n) {
    nfolds = n < 10 ? n : 10;
    std::cout << "Number of folds is too large, we'll set it to be: " << nfolds << std::endl;
  }
  int size = n / nfolds;
  arma::vec YPred = arma::zeros(n);
  arma::vec beta = arma::zeros(X.n_cols);
  arma::vec mse = arma::zeros(nlambda);
  for (int i = 0; i < nlambda; i++) {
    for (int j = 0; j < nfolds; j++) {
      int low = j * size;
      int up = (j == (nfolds - 1)) ? (n - 1) : ((j + 1) * size - 1);
      arma::uvec idx = getIndex(n, low, up);
      arma::uvec idxComp = getIndexComp(n, low, up);
      Rcpp::List listILAMM = ncvxReg(X.rows(idxComp), Y.rows(idxComp), lambdaSeq(i), penalty,
                                     phi0, gamma, epsilon_c, epsilon_t, iteMax, intercept, true);
      arma::vec betaHat = Rcpp::as<arma::vec>(listILAMM["beta"]);
      YPred.rows(idx) = X.rows(idx) * betaHat;
    }
    mse(i) = norm(Y - YPred, 2);
  }
  arma::uword cvIdx = mse.index_min();
  Rcpp::List listILAMM = ncvxReg(X, Y, lambdaSeq(cvIdx), penalty, phi0, gamma, epsilon_c,
                                 epsilon_t, iteMax, intercept, true);
  beta = Rcpp::as<arma::vec>(listILAMM["beta"]);
  return Rcpp::List::create(Rcpp::Named("beta") = beta, Rcpp::Named("penalty") = penalty,
                            Rcpp::Named("lambdaSeq") = lambdaSeq, Rcpp::Named("mse") = mse,
                            Rcpp::Named("lambdaMin") = lambdaSeq(cvIdx), Rcpp::Named("nfolds") = nfolds);
}

//' The function performs k-fold cross validation for (high-dimensional) Huber regularized regression with non-convex penalties: Lasso, SCAD and MCP, and it's implemented by I-LAMM algorithm.
//'
//' The design matrix \eqn{X} can be either high-dimensional or low-dimensional, its number of rows should be the same as the length of \eqn{Y}. The sequence of \eqn{\lambda}'s and \eqn{\tau}'s have default settings but they can be user-specified. All the arguments except for \eqn{X} and \eqn{Y} have default settings.
//'
//' @title K-fold cross validation for non-convex regularized Huber regression
//' @param X An \eqn{n} by \eqn{d} design matrix with each row being a sample and each column being a variable, either low-dimensional data (\eqn{d \le n}) or high-dimensional data (\eqn{d > n}) are allowed.
//' @param Y A continuous response vector with length \eqn{n}.
//' @param lSeq Sequence of tuning parameter of regularized regression \eqn{\lambda}, every element should be positive. If it's not specified, the default sequence is generated in this way: define \eqn{\lambda_max = max(|Y^T X|) / n}, and \eqn{\lambda_min = 0.01 * \lambda_max}, then \code{lseq} is a sequence from \eqn{\lambda_max} to \eqn{\lambda_min} that decreases uniformly on log scale.
//' @param nlambda Number of \eqn{\lambda} to generate the default sequence \code{lSeq}. It's not necessary if \code{lSeq} is specified. The default value is 30.
//' @param penalty Type of non-convex penalties with default setting "SCAD", possible choices are: "Lasso", "SCAD" and "MCP".
//' @param tSeq Sequence of robustness parameter of Huber loss \eqn{\tau}, every element should be positive. If it's not specified, the default sequence is generated in this way: define \eqn{R} as the residual from Lasso by fitting \code{cvNcvxReg} with \code{lSeq}, and \eqn{\sigma_MAD = median(|R - median(R)|) / \Phi^(-1)(3/4)} is the median absolute deviation estimator, then \code{tSeq} = \eqn{2^j * \sigma_MAD \sqrt(n / log(nd))}, where \eqn{j} are integers from -\code{ntau}/2 to \code{ntau}/2.
//' @param ntau Number of \eqn{\tau} to generate the default sequence \code{tSeq}. It's not necessary if \code{tSeq} is specified. The default value is 5.
//' @param phi0 The initial value of the isotropic parameter \eqn{\phi} in I-LAMM algorithm. The defalut value is 0.001.
//' @param gamma The inflation parameter in I-LAMM algorithm, in each iteration of I-LAMM, we will inflate \eqn{\phi} by \eqn{\gamma}. The defalut value is 1.5.
//' @param epsilon_c The tolerance level for contraction stage, iteration of contraction will stop when \eqn{||\beta_new - \beta_old||_2 / \sqrt(d + 1) < \epsilon_c}. The defalut value is 1e-4.
//' @param epsilon_t The tolerance level for tightening stage, iteration of tightening will stop when \eqn{||\beta_new - \beta_old||_2 / \sqrt(d + 1) < \epsilon_t}. The defalut value is 1e-4.
//' @param iteMax The maximal number of iteration in either contraction or tightening stage, if this number is reached, the convergence of I-LAMM is failed. The defalut value is 500.
//' @param nfolds The number of folds to conduct cross validation, values that are greater than 10 are not recommended, and it'll be modified to 10 if the input is greater than 10. The default value is 3.
//' @param intercept Boolean value indicating whether an intercept term should be included into the model. The default setting is \code{FALSE}.
//' @param itcpIncluded Boolean value indicating whether a column of 1's has been included in the design matrix \eqn{X}. The default setting is \code{FALSE}.
//' @return A list including the following terms will be returned:
//' \itemize{
//' \item \code{beta} The estimated \eqn{\beta} with \eqn{\lambda} and \eqn{\tau} determined by cross validation, it's a vector with length d + 1, with the first one being the value of intercept (0 if \code{intercept = FALSE}).
//' \item \code{penalty} The type of penalty.
//' \item \code{lambdaSeq} The sequence of \eqn{\lambda}'s for cross validation.
//' \item \code{tauSeq} The sequence of \eqn{\tau}'s for cross validation.
//' \item \code{mse} The mean squared error from cross validation, it's a matrix with dimension \code{nlambda} by \code{ntau}.
//' \item \code{lambdaMin} The value of \eqn{\lambda} in \code{lSeq} that minimized \code{mse}.
//' \item \code{tauMin} The value of \eqn{\tau} in \code{tSeq} that minimized \code{mse}.
//' \item \code{nfolds} The number of folds for cross validation.
//' }
//' @author Xiaoou Pan, Qiang Sun, Wen-Xin Zhou
//' @references Fan, J., Liu, H., Sun, Q. and Zhang, T. (2018). I-LAMM for sparse learning: Simultaneous control of algorithmic complexity and statistical error. Ann. Statist. 46 814–841.
//' @seealso \code{\link{ncvxHuberReg}}
//' @examples
//' n = 50
//' d = 100
//' set.seed(2018)
//' X = matrix(rnorm(n * d), n, d)
//' beta = c(rep(2, 3), rep(0, d - 3))
//' Y = X %*% beta + rlnorm(n, 0, 1.2) - exp(1.2^2 / 2)
//' # Fit SCAD without intercept, with lambda and tau determined by 3-folds cross validation
//' fit = cvNcvxHuberReg(X, Y)
//' fit$beta
//' fit$lambdaMin
//' fit$tauMin
//' # Fit MCP with intercept, with lambda and tau determined by 3-folds cross validation
//' fit = cvNcvxHuberReg(X, Y, penalty = "MCP", intercept = TRUE)
//' fit$beta
//' fit$lambdaMin
//' fit$tauMin
//' @export
// [[Rcpp::export]]
Rcpp::List cvNcvxHuberReg(arma::mat& X, const arma::vec& Y,
                  Rcpp::Nullable<Rcpp::NumericVector> lSeq = R_NilValue, int nlambda = 30,
                  const std::string penalty = "SCAD",
                  Rcpp::Nullable<Rcpp::NumericVector> tSeq = R_NilValue, int ntau = 5,
                  const double phi0 = 0.001, const double gamma = 1.5,
                  const double epsilon_c = 0.0001, const double epsilon_t = 0.0001,
                  const int iteMax = 500, int nfolds = 3, const bool intercept = false,
                  const bool itcpIncluded = false) {
  if (!itcpIncluded) {
    arma::mat XX = arma::ones(X.n_rows, X.n_cols + 1);
    XX.cols(1, X.n_cols) = X;
    X = XX;
  }
  int n = Y.size();
  int d = X.n_cols - 1;
  arma::vec lambdaSeq = arma::vec();
  if (lSeq.isNotNull()) {
    lambdaSeq = Rcpp::as<arma::vec>(lSeq);
    nlambda = lambdaSeq.size();
  } else {
    double lambdaMax = max(abs(Y.t() * X)) / n;
    double lambdaMin = 0.01 * lambdaMax;
    lambdaSeq = exp(arma::linspace(std::log((long double)lambdaMin),
                    std::log((long double)lambdaMax), nlambda));
  }
  arma::vec tauSeq = arma::vec();
  if (tSeq.isNotNull()) {
    tauSeq = Rcpp::as<arma::vec>(tSeq);
    ntau = tauSeq.size();
  } else {
    Rcpp::List listILAMM = cvNcvxReg(X, Y, lSeq, nlambda, "Lasso", phi0, gamma, epsilon_c,
                                     epsilon_t, iteMax, nfolds, intercept, true);
    arma::vec betaLasso = Rcpp::as<arma::vec>(listILAMM["beta"]);
    arma::vec Yhat = X * betaLasso;
    arma::vec res = Y - Yhat;
    double sigmaHat = median(abs(res - median(res))) / 0.6745;
    arma::vec tauCon = tauConst(ntau);
    tauSeq = sigmaHat * std::sqrt((long double)(n / std::log(n * d))) * tauCon;
  }
  if (nfolds > 10 || nfolds > n) {
    nfolds = n < 10 ? n : 10;
    std::cout << "Number of folds is too big, we'll set it to be: " << nfolds << std::endl;
  }
  int size = n / nfolds;
  arma::vec YPred = arma::zeros(n);
  arma::vec beta = arma::zeros(X.n_cols);
  arma::mat mse = arma::zeros(nlambda, ntau);
  for (int i = 0; i < nlambda; i++) {
    for (int k = 0; k < ntau; k++) {
      for (int j = 0; j < nfolds; j++) {
        int low = j * size;
        int up = (j == (nfolds - 1)) ? (n - 1) : ((j + 1) * size - 1);
        arma::uvec idx = getIndex(n, low, up);
        arma::uvec idxComp = getIndexComp(n, low, up);
        Rcpp::List listILAMM = ncvxHuberReg(X.rows(idxComp), Y.rows(idxComp), lambdaSeq(i),
                                            penalty, tauSeq(k), phi0, gamma, epsilon_c, epsilon_t,
                                            iteMax, intercept, true);
        arma::vec betaHat = Rcpp::as<arma::vec>(listILAMM["beta"]);
        YPred.rows(idx) = X.rows(idx) * betaHat;
      }
      mse(i, k) = norm(Y - YPred, 2);
    }
  }
  arma::uword cvIdx = mse.index_min();
  arma::uword idxLambda = cvIdx - (cvIdx / nlambda) * nlambda;
  arma::uword idxTau = cvIdx / nlambda;
  Rcpp::List listILAMM = ncvxHuberReg(X, Y, lambdaSeq(idxLambda), penalty, tauSeq(idxTau), phi0,
                                      gamma, epsilon_c, epsilon_t, iteMax, intercept, true);
  beta = Rcpp::as<arma::vec>(listILAMM["beta"]);
  return Rcpp::List::create(Rcpp::Named("beta") = beta, Rcpp::Named("penalty") = penalty,
                            Rcpp::Named("lambdaSeq") = lambdaSeq, Rcpp::Named("tauSeq") = tauSeq,
                            Rcpp::Named("mse") = mse, Rcpp::Named("lambdaMin") = lambdaSeq(idxLambda),
                            Rcpp::Named("tauMin") = tauSeq(idxTau), Rcpp::Named("nfolds") = nfolds);
}
