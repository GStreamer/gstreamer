<?xml version='1.0'?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                version='1.0'
                xmlns="http://www.w3.org/TR/xhtml1/transitional"
                exclude-result-prefixes="#default">

<!-- This file is for customizing the default XSL stylesheets. -->
<!-- We include them here (this one is for print output):      -->
<xsl:import
 href="http://docbook.sourceforge.net/release/xsl/1.50.0/fo/docbook.xsl"/>

<!-- .. and customize them here:                               -->
<xsl:include href="ulink.xsl"/>
<xsl:include href="keycombo.xsl"/>
<xsl:param name="body.font.family" select="'serif'"/>
<xsl:param name="sans.font.family" select="'sans-serif'"/>
<xsl:param name="title.font.family" select="'sans-serif'"/>

</xsl:stylesheet>
