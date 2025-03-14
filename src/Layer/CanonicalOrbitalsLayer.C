/*******************************************************************************

  Copyright (C) 2011-2015 Andrew Gilbert

  This file is part of IQmol, a free molecular visualization program. See
  <http://iqmol.org> for more details.

  IQmol is free software: you can redistribute it and/or modify it under the
  terms of the GNU General Public License as published by the Free Software
  Foundation, either version 3 of the License, or (at your option) any later
  version.

  IQmol is distributed in the hope that it will be useful, but WITHOUT ANY
  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
  details.

  You should have received a copy of the GNU General Public License along
  with IQmol.  If not, see <http://www.gnu.org/licenses/>.

********************************************************************************/

#include <QProgressDialog>
#include "CanonicalOrbitalsLayer.h"
#include "GridProduct.h"
#include "QsLog.h"


using namespace qglviewer;

namespace IQmol {
namespace Layer {

CanonicalOrbitals::CanonicalOrbitals(Data::CanonicalOrbitals& canonicalOrbitals)
 : Orbitals(canonicalOrbitals), m_canonicalOrbitals(canonicalOrbitals), m_progressDialog(0),
   m_gridProduct(0)
{
   if (orbitalType() == Data::Orbitals::Canonical) {
      computeDensityVectors();
   }
   m_availableDensities.append(m_canonicalOrbitals.densityList());
   qDebug() << "Number of available densities" << m_availableDensities.size();
}



double CanonicalOrbitals::alphaOrbitalEnergy(unsigned const i) const 
{ 
   return m_canonicalOrbitals.alphaOrbitalEnergy(i);
}



double CanonicalOrbitals::betaOrbitalEnergy(unsigned const i) const 
{ 
   return m_canonicalOrbitals.betaOrbitalEnergy(i);
}



void CanonicalOrbitals::computeDensityVectors()
{
   using namespace boost::numeric::ublas;

   unsigned N(nBasis());
   unsigned Na(nAlpha());
   unsigned Nb(nBeta());

   Matrix const& alphaCoefficients(m_orbitals.alphaCoefficients());
   Matrix const& betaCoefficients(m_orbitals.betaCoefficients());

   Matrix coeffs(Na, N);
   Matrix Pa(N, N);
   Matrix Pb(N, N);

   for (unsigned i = 0; i < Na; ++i) {
       for (unsigned j = 0; j < N; ++j) {
           coeffs(i,j) = alphaCoefficients(i,j);  
       }
   }

   noalias(Pa) = prod(trans(coeffs), coeffs);

   Data::SurfaceType alpha(Data::SurfaceType::AlphaDensity);
   m_availableDensities.append(new Data::Density(alpha, Pa, "Alpha Density"));

   coeffs.resize(Nb, N);

   for (unsigned i = 0; i < Nb; ++i) {
       for (unsigned j = 0; j < N; ++j) {
           coeffs(i,j) = betaCoefficients(i,j);
       }
   }

   noalias(Pb) = prod(trans(coeffs), coeffs);

   Data::SurfaceType beta(Data::SurfaceType::BetaDensity);
   m_availableDensities.append(new Data::Density(beta, Pb, "Beta Density"));

   Data::SurfaceType total(Data::SurfaceType::TotalDensity);
   m_availableDensities.append(new Data::Density(total, Pa+Pb, "Total Density"));

   Data::SurfaceType spin(Data::SurfaceType::SpinDensity);
   m_availableDensities.append(new Data::Density(spin, Pa-Pb, "Spin Density"));

   // Mulliken densities
   Data::ShellList const&  shells(m_canonicalOrbitals.shellList());
   QList<unsigned> offsets(shells.basisAtomOffsets());
   unsigned nAtoms(offsets.size());

   offsets.append(N);
   Matrix Mull(Pa+Pb);

   for (unsigned atom = 0; atom < nAtoms; ++atom) {
       unsigned begin(offsets[atom]);
       unsigned end(offsets[atom+1]);
       for (unsigned i = begin; i < end; ++i) {
           for (unsigned j = begin; j < end; ++j) {
               Mull(i,j) = 0;
           }
       }
   }

   Data::SurfaceType mulliken2(Data::SurfaceType::MullikenDiatomic);
   m_availableDensities.append(new Data::Density(mulliken2, Mull, "Mulliken Diatomic Density"));

   Mull = Pa + Pb - Mull;

   Data::SurfaceType mulliken(Data::SurfaceType::MullikenAtomic);
   m_availableDensities.append(new Data::Density(mulliken, Mull, "Mulliken Atomic Density"));
}



QString CanonicalOrbitals::description(Data::SurfaceInfo const& info, 
   bool const tooltip)
{
   Data::SurfaceType const& type(info.type());
   QString label;

   if (type.isOrbital()) {
      unsigned index(type.index());
      bool     isAlpha(type.kind() == Data::SurfaceType::AlphaOrbital);

      label = m_canonicalOrbitals.label(index, isAlpha);

      if (tooltip) {
         double orbitalEnergy(0.0);
         if (isAlpha) {
            orbitalEnergy = m_canonicalOrbitals.alphaOrbitalEnergy(index);
         }else {
            orbitalEnergy = m_canonicalOrbitals.betaOrbitalEnergy(index);
         }
         label += "\nEnergy   = " + QString::number(orbitalEnergy, 'f', 3);
       }
   }else {
      // density
      label = type.toString();
   }

   if (tooltip) label += "\nIsovalue = " + QString::number(info.isovalue(), 'f', 3);
 
   return label;
}



void CanonicalOrbitals::computeFirstOrderDensityMatrix()
{
   unsigned Na(nAlpha());

   // Just restricted for now
   QList<Data::GridData const*> orbitalGrids(findGrids(Data::SurfaceType::AlphaOrbital));

   if (orbitalGrids.size() != (int)Na) {
      QLOG_ERROR() << "Not all orbitals available";
      return;
   }

   // Assume we have all the occupieds 

   double const binSize(0.1);
   m_gridProduct = new GridProduct(m_values, orbitalGrids, binSize);

   m_progressDialog = new QProgressDialog();
   m_progressDialog->setWindowModality(Qt::NonModal);
   m_progressDialog->show();
   m_progressDialog->setMaximum(m_gridProduct->totalProgress());

   connect(m_gridProduct, SIGNAL(progressValue(int)), 
      m_progressDialog, SLOT(setValue(int)));

   connect(m_gridProduct, SIGNAL(finished()), 
      this, SLOT(firstOrderDensityMatrixFinished()));

      QLOG_INFO() << "Starting grid calc";
   m_gridProduct->start();
}



void CanonicalOrbitals::firstOrderDensityMatrixFinished()
{
    m_progressDialog->deleteLater();
    m_progressDialog = 0;

    double const binSize(0.1);

    for (unsigned i(0); i < m_values.size(); ++i) {
        qDebug() <<  i*binSize << "  " << m_values[i];
    }
    
}


} } // end namespace IQmol::Layer
